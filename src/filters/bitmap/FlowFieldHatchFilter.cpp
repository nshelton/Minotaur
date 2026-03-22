#include "filters/bitmap/FlowFieldHatchFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline size_t idx(int x, int y, int W)
{
    return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
}

// Two-pass Chamfer distance transform (same as ConcentricOutlineFilter)
void computeDistanceTransform(const std::vector<uint8_t> &binary,
                              std::vector<int> &distMap, int W, int H)
{
    const int INF = W + H;
    distMap.assign(static_cast<size_t>(W * H), INF);

    // Forward pass
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const size_t i = idx(x, y, W);
            if (binary[i] == 0)
            {
                distMap[i] = 0;
            }
            else
            {
                if (y > 0)
                    distMap[i] = std::min(distMap[i], distMap[idx(x, y - 1, W)] + 1);
                if (x > 0)
                    distMap[i] = std::min(distMap[i], distMap[idx(x - 1, y, W)] + 1);
            }
        }
    }

    // Backward pass
    for (int y = H - 1; y >= 0; --y)
    {
        for (int x = W - 1; x >= 0; --x)
        {
            const size_t i = idx(x, y, W);
            if (binary[i] != 0)
            {
                if (y < H - 1)
                    distMap[i] = std::min(distMap[i], distMap[idx(x, y + 1, W)] + 1);
                if (x < W - 1)
                    distMap[i] = std::min(distMap[i], distMap[idx(x + 1, y, W)] + 1);
            }
        }
    }
}

// Compute flow field using Sobel tangent
void computeSobelFlow(const Bitmap &in, int W, int H,
                      float flowStrength, float defaultDx, float defaultDy,
                      std::vector<float> &flowX, std::vector<float> &flowY)
{
    flowX.assign(static_cast<size_t>(W * H), defaultDx);
    flowY.assign(static_cast<size_t>(W * H), defaultDy);

    for (int y = 1; y < H - 1; ++y)
    {
        for (int x = 1; x < W - 1; ++x)
        {
            const int tl = in.pixels[idx(x - 1, y - 1, W)];
            const int tc = in.pixels[idx(x,     y - 1, W)];
            const int tr = in.pixels[idx(x + 1, y - 1, W)];
            const int ml = in.pixels[idx(x - 1, y,     W)];
            const int mr = in.pixels[idx(x + 1, y,     W)];
            const int bl = in.pixels[idx(x - 1, y + 1, W)];
            const int bc = in.pixels[idx(x,     y + 1, W)];
            const int br = in.pixels[idx(x + 1, y + 1, W)];

            const float gx = static_cast<float>(-tl - 2 * ml - bl + tr + 2 * mr + br);
            const float gy = static_cast<float>(-tl - 2 * tc - tr + bl + 2 * bc + br);
            const float mag = std::sqrt(gx * gx + gy * gy);

            float tx, ty;
            if (mag > 5.0f)
            {
                // Tangent = perpendicular to gradient
                tx = -gy / mag;
                ty =  gx / mag;

                // Ensure consistent direction: pick the tangent that aligns
                // with the default hatch direction (dot product > 0)
                const float dot = tx * defaultDx + ty * defaultDy;
                if (dot < 0.0f) { tx = -tx; ty = -ty; }

                // Blend with default direction
                tx = flowStrength * tx + (1.0f - flowStrength) * defaultDx;
                ty = flowStrength * ty + (1.0f - flowStrength) * defaultDy;
            }
            else
            {
                tx = defaultDx;
                ty = defaultDy;
            }

            // Normalize
            const float len = std::sqrt(tx * tx + ty * ty);
            if (len > 1e-6f)
            {
                tx /= len;
                ty /= len;
            }

            const size_t i = idx(x, y, W);
            flowX[i] = tx;
            flowY[i] = ty;
        }
    }
}

// Compute flow field using clamped distance field tangent
void computeClampedDistanceFlow(const Bitmap &in, int W, int H,
                                int threshold, int clampRadius,
                                float flowStrength, float defaultDx, float defaultDy,
                                std::vector<float> &flowX, std::vector<float> &flowY)
{
    flowX.assign(static_cast<size_t>(W * H), defaultDx);
    flowY.assign(static_cast<size_t>(W * H), defaultDy);

    // Binarize: dark pixels = foreground (255)
    std::vector<uint8_t> binary(static_cast<size_t>(W * H), 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            binary[idx(x, y, W)] = (in.pixels[idx(x, y, W)] <= threshold) ? 255 : 0;

    // Distance transform
    std::vector<int> distMap;
    computeDistanceTransform(binary, distMap, W, H);

    // Clamp the distance field
    std::vector<float> distClamped(static_cast<size_t>(W * H));
    for (size_t i = 0; i < distClamped.size(); ++i)
        distClamped[i] = std::min(static_cast<float>(distMap[i]),
                                  static_cast<float>(clampRadius));

    // Compute gradient of clamped distance field via central differences,
    // then take tangent (perpendicular)
    for (int y = 1; y < H - 1; ++y)
    {
        for (int x = 1; x < W - 1; ++x)
        {
            const float gx = (distClamped[idx(x + 1, y, W)] - distClamped[idx(x - 1, y, W)]) * 0.5f;
            const float gy = (distClamped[idx(x, y + 1, W)] - distClamped[idx(x, y - 1, W)]) * 0.5f;
            const float mag = std::sqrt(gx * gx + gy * gy);

            float tx, ty;
            if (mag > 1e-4f)
            {
                // Tangent = perpendicular to gradient
                tx = -gy / mag;
                ty =  gx / mag;

                const float dot = tx * defaultDx + ty * defaultDy;
                if (dot < 0.0f) { tx = -tx; ty = -ty; }

                tx = flowStrength * tx + (1.0f - flowStrength) * defaultDx;
                ty = flowStrength * ty + (1.0f - flowStrength) * defaultDy;
            }
            else
            {
                // Gradient is zero (flat clamped region) -> use default angle
                tx = defaultDx;
                ty = defaultDy;
            }

            const float len = std::sqrt(tx * tx + ty * ty);
            if (len > 1e-6f)
            {
                tx /= len;
                ty /= len;
            }

            const size_t i = idx(x, y, W);
            flowX[i] = tx;
            flowY[i] = ty;
        }
    }
}

} // anonymous namespace

void FlowFieldHatchFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
    out.paths.clear();
    out.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

    const int W = static_cast<int>(in.width_px);
    const int H = static_cast<int>(in.height_px);
    if (W <= 0 || H <= 0 || in.pixels.empty())
    {
        out.computeAABB();
        return;
    }

    // Read parameters
    const int flowType = static_cast<int>(std::lround(m_parameters.at("flow_field_type").value));
    const int threshold = clampi(static_cast<int>(std::lround(m_parameters.at("threshold").value)), 0, 255);
    const float seedSpacing = std::max(2.0f, m_parameters.at("seed_spacing_px").value);
    const float maxConnDist = std::max(1.0f, m_parameters.at("max_connect_distance_px").value);
    const float flowStrength = std::clamp(m_parameters.at("flow_strength").value, 0.0f, 1.0f);
    const float angleDeg = m_parameters.at("hatch_angle_deg").value;
    const int clampRadius = std::max(1, static_cast<int>(std::lround(m_parameters.at("clamp_radius_px").value)));

    // Default hatch direction
    float ang = std::fmod(angleDeg, 360.0f);
    if (ang < 0.0f) ang += 360.0f;
    const float theta = ang * 0.017453292519943295f;
    const float defaultDx = std::cos(theta);
    const float defaultDy = std::sin(theta);

    // --- Phase 1: Compute flow field ---
    std::vector<float> flowX, flowY;
    if (flowType == 1)
    {
        computeClampedDistanceFlow(in, W, H, threshold, clampRadius,
                                   flowStrength, defaultDx, defaultDy,
                                   flowX, flowY);
    }
    else
    {
        computeSobelFlow(in, W, H, flowStrength, defaultDx, defaultDy,
                         flowX, flowY);
    }

    // --- Phase 2: Place seed nodes on dark pixels ---
    std::vector<Vec2> seeds;
    const int spacingI = std::max(2, static_cast<int>(std::round(seedSpacing)));

    for (int y = spacingI / 2; y < H; y += spacingI)
    {
        for (int x = spacingI / 2; x < W; x += spacingI)
        {
            const uint8_t v = in.pixels[idx(x, y, W)];
            if (v <= threshold)
            {
                seeds.emplace_back(static_cast<float>(x), static_cast<float>(y));
            }
        }
    }

    if (seeds.empty())
    {
        out.computeAABB();
        return;
    }

    // --- Phase 3: Grid-based spatial lookup and chain seeds along flow field ---
    // Build a grid where each cell holds indices of seeds in that cell.
    // Cell size = maxConnDist so we only need to search a 3x3 neighbourhood.
    const float cellSize = std::max(1.0f, maxConnDist);
    const int gridW = static_cast<int>(std::ceil(static_cast<float>(W) / cellSize));
    const int gridH = static_cast<int>(std::ceil(static_cast<float>(H) / cellSize));
    std::vector<std::vector<int>> grid(static_cast<size_t>(gridW) * static_cast<size_t>(gridH));

    for (int i = 0; i < static_cast<int>(seeds.size()); ++i)
    {
        const int cx = clampi(static_cast<int>(seeds[i].x / cellSize), 0, gridW - 1);
        const int cy = clampi(static_cast<int>(seeds[i].y / cellSize), 0, gridH - 1);
        grid[static_cast<size_t>(cy) * static_cast<size_t>(gridW) + static_cast<size_t>(cx)].push_back(i);
    }

    // Lambda: find nearest unused seed to a target point within maxConnDist
    std::vector<bool> used(seeds.size(), false);
    const float maxConnDist2 = maxConnDist * maxConnDist;

    auto findNearest = [&](const Vec2 &target) -> int {
        const int cx = static_cast<int>(target.x / cellSize);
        const int cy = static_cast<int>(target.y / cellSize);

        int bestIdx = -1;
        float bestD2 = maxConnDist2;

        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int gx = cx + dx;
                const int gy = cy + dy;
                if (gx < 0 || gx >= gridW || gy < 0 || gy >= gridH) continue;

                const auto &cell = grid[static_cast<size_t>(gy) * static_cast<size_t>(gridW) + static_cast<size_t>(gx)];
                for (int si : cell)
                {
                    if (used[si]) continue;
                    const float ddx = seeds[si].x - target.x;
                    const float ddy = seeds[si].y - target.y;
                    const float d2 = ddx * ddx + ddy * ddy;
                    if (d2 < bestD2)
                    {
                        bestD2 = d2;
                        bestIdx = si;
                    }
                }
            }
        }
        return bestIdx;
    };

    size_t totalVertices = 0;
    size_t totalPaths = 0;

    for (int i = 0; i < static_cast<int>(seeds.size()); ++i)
    {
        if (used[i]) continue;

        Path path;
        path.closed = false;

        int current = i;
        used[current] = true;
        path.points.emplace_back(
            seeds[current].x * in.pixel_size_mm,
            seeds[current].y * in.pixel_size_mm
        );

        // Chain forward along the flow field
        while (true)
        {
            const Vec2 &pos = seeds[current];
            const int px = clampi(static_cast<int>(std::round(pos.x)), 0, W - 1);
            const int py = clampi(static_cast<int>(std::round(pos.y)), 0, H - 1);
            const size_t fi = idx(px, py, W);

            const float fdx = flowX[fi];
            const float fdy = flowY[fi];

            // Search target: step along flow direction
            Vec2 target(pos.x + fdx * seedSpacing, pos.y + fdy * seedSpacing);

            const int bestKey = findNearest(target);
            if (bestKey < 0) break;

            used[bestKey] = true;
            path.points.emplace_back(
                seeds[bestKey].x * in.pixel_size_mm,
                seeds[bestKey].y * in.pixel_size_mm
            );
            current = bestKey;
        }

        if (path.points.size() >= 2)
        {
            totalVertices += path.points.size();
            ++totalPaths;
            out.paths.emplace_back(std::move(path));
        }
    }

    const_cast<FlowFieldHatchFilter *>(this)->setLastVertexCount(totalVertices);
    const_cast<FlowFieldHatchFilter *>(this)->setLastPathCount(totalPaths);

    out.computeAABB();
}
