#include "filters/bitmap/LineHatchFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
    static inline float clampf(float v, float lo, float hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }

    static inline int clampi(int v, int lo, int hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }
}

void LineHatchFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
    out.paths.clear();
    out.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

    const int width = static_cast<int>(in.width_px);
    const int height = static_cast<int>(in.height_px);
    if (width <= 0 || height <= 0 || in.pixels.empty())
    {
        out.computeAABB();
        return;
    }

    const float stepPx = std::max(1.0f, std::floor(m_parameters.at("step_px").value));
    const float angleDeg = m_parameters.at("angle_deg").value;
    const int threshold = clampi(static_cast<int>(std::lround(m_parameters.at("threshold").value)), 0, 255);

    // Normalize angle to [0, 360)
    float ang = std::fmod(angleDeg, 360.0f);
    if (ang < 0.0f) ang += 360.0f;
    const float theta = ang * 0.017453292519943295f; // pi/180

    const float dx = std::cos(theta);
    const float dy = std::sin(theta);
    // Perpendicular unit (normal)
    const float nx = -dy;
    const float ny = dx;

    auto isDark = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        const uint8_t v = in.pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
        // Consider dark if <= threshold
        return v <= threshold;
    };

    // Project rectangle corners on the normal to get scan range (in pixel space)
    const float corners[4][2] = {{0.0f, 0.0f}, {static_cast<float>(width), 0.0f}, {static_cast<float>(width), static_cast<float>(height)}, {0.0f, static_cast<float>(height)}};
    float sMin = std::numeric_limits<float>::infinity();
    float sMax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 4; ++i)
    {
        const float cx = corners[i][0];
        const float cy = corners[i][1];
        const float s = cx * nx + cy * ny;
        if (s < sMin) sMin = s;
        if (s > sMax) sMax = s;
    }

    const float eps = 1e-6f;
    auto addIntersections = [&](float s) -> std::vector<Vec2> {
        std::vector<Vec2> pts;
        pts.reserve(4);
        // Intersect with x = 0 and x = width
        if (std::fabs(ny) > eps)
        {
            const float y0 = (s - nx * 0.0f) / ny;
            if (y0 >= 0.0f && y0 <= static_cast<float>(height)) pts.emplace_back(0.0f, y0);
            const float yW = (s - nx * static_cast<float>(width)) / ny;
            if (yW >= 0.0f && yW <= static_cast<float>(height)) pts.emplace_back(static_cast<float>(width), yW);
        }
        // Intersect with y = 0 and y = height
        if (std::fabs(nx) > eps)
        {
            const float x0 = (s - ny * 0.0f) / nx;
            if (x0 >= 0.0f && x0 <= static_cast<float>(width)) pts.emplace_back(x0, 0.0f);
            const float xH = (s - ny * static_cast<float>(height)) / nx;
            if (xH >= 0.0f && xH <= static_cast<float>(width)) pts.emplace_back(xH, static_cast<float>(height));
        }

        // Deduplicate near-coincident points
        std::vector<Vec2> unique;
        for (const auto &p : pts)
        {
            bool dup = false;
            for (const auto &q : unique)
            {
                const float dxp = q.x - p.x;
                const float dyp = q.y - p.y;
                if (dxp * dxp + dyp * dyp < 1e-6f) { dup = true; break; }
            }
            if (!dup) unique.push_back(p);
        }
        if (unique.size() > 2) unique.resize(2);
        return unique;
    };

    size_t totalVertices = 0;
    size_t totalPaths = 0;

    auto emitRuns = [&](const Vec2 &a, const Vec2 &b) {
        const float L = std::hypot(b.x - a.x, b.y - a.y);
        if (L < 1.0f) return;
        const int samples = std::max(2, static_cast<int>(std::ceil(L)));
        int runStart = -1;
        for (int i = 0; i < samples; ++i)
        {
            const float t = (samples == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(samples - 1);
            const float px = a.x + (b.x - a.x) * t;
            const float py = a.y + (b.y - a.y) * t;
            const int xi = clampi(static_cast<int>(std::floor(px)), 0, width - 1);
            const int yi = clampi(static_cast<int>(std::floor(py)), 0, height - 1);
            if (isDark(xi, yi))
            {
                if (runStart < 0) runStart = i;
            }
            else if (runStart >= 0)
            {
                const float t0 = static_cast<float>(runStart) / static_cast<float>(samples - 1);
                const float t1 = static_cast<float>(i - 1) / static_cast<float>(samples - 1);
                const float x0 = a.x + (b.x - a.x) * t0;
                const float y0 = a.y + (b.y - a.y) * t0;
                const float x1 = a.x + (b.x - a.x) * t1;
                const float y1 = a.y + (b.y - a.y) * t1;
                Path seg;
                seg.closed = false;
                seg.points.emplace_back(x0 * in.pixel_size_mm, y0 * in.pixel_size_mm);
                seg.points.emplace_back(x1 * in.pixel_size_mm, y1 * in.pixel_size_mm);
                totalVertices += 2;
                ++totalPaths;
                out.paths.emplace_back(std::move(seg));
                runStart = -1;
            }
        }
        if (runStart >= 0)
        {
            const float t0 = static_cast<float>(runStart) / static_cast<float>(samples - 1);
            const float x0 = a.x + (b.x - a.x) * t0;
            const float y0 = a.y + (b.y - a.y) * t0;
            Path seg;
            seg.closed = false;
            seg.points.emplace_back(x0 * in.pixel_size_mm, y0 * in.pixel_size_mm);
            seg.points.emplace_back(b.x * in.pixel_size_mm, b.y * in.pixel_size_mm);
            totalVertices += 2;
            ++totalPaths;
            out.paths.emplace_back(std::move(seg));
        }
    };

    // Sweep across the image with parallel lines spaced by stepPx along the normal
    const int sStart = static_cast<int>(std::floor(sMin));
    const int sEnd = static_cast<int>(std::ceil(sMax));
    for (float s = static_cast<float>(sStart); s <= static_cast<float>(sEnd); s += stepPx)
    {
        const std::vector<Vec2> ints = addIntersections(s);
        if (ints.size() < 2) continue;
        emitRuns(ints[0], ints[1]);
    }

    out.computeAABB();
}


