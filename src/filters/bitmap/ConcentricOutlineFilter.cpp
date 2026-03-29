#include "filters/bitmap/ConcentricOutlineFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <queue>
#include <limits>
#include <set>

namespace {
    static inline int clampi(int v, int lo, int hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }

    static inline size_t idxOf(int x, int y, int W)
    {
        return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
    }

    // Fast distance transform using two-pass algorithm
    // Returns distance map where each pixel contains its distance from the nearest background pixel
    void computeDistanceTransform(const std::vector<uint8_t> &binary, std::vector<int> &distMap, int W, int H)
    {
        const int INF = W + H; // Large enough value
        distMap.assign(static_cast<size_t>(W * H), INF);

        // Forward pass (top-left to bottom-right)
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const size_t idx = idxOf(x, y, W);

                if (binary[idx] == 0) // background
                {
                    distMap[idx] = 0;
                }
                else
                {
                    // Check neighbors (up, left)
                    if (y > 0)
                        distMap[idx] = std::min(distMap[idx], distMap[idxOf(x, y-1, W)] + 1);
                    if (x > 0)
                        distMap[idx] = std::min(distMap[idx], distMap[idxOf(x-1, y, W)] + 1);
                }
            }
        }

        // Backward pass (bottom-right to top-left)
        for (int y = H - 1; y >= 0; --y)
        {
            for (int x = W - 1; x >= 0; --x)
            {
                const size_t idx = idxOf(x, y, W);

                if (binary[idx] != 0) // foreground
                {
                    // Check neighbors (down, right)
                    if (y < H - 1)
                        distMap[idx] = std::min(distMap[idx], distMap[idxOf(x, y+1, W)] + 1);
                    if (x < W - 1)
                        distMap[idx] = std::min(distMap[idx], distMap[idxOf(x+1, y, W)] + 1);
                }
            }
        }
    }

    // Extract boundary pixels at a specific distance level
    void extractLevelBoundary(const std::vector<int> &distMap, int level, std::vector<uint8_t> &boundary, int W, int H)
    {
        boundary.assign(static_cast<size_t>(W * H), 0);

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const size_t idx = idxOf(x, y, W);
                const int d = distMap[idx];

                // A pixel is on the boundary if it's at this level AND has a neighbor at level-1
                if (d == level)
                {
                    bool isBoundary = false;

                    // Check 4-neighbors
                    const int dx[4] = {0, -1, 1, 0};
                    const int dy[4] = {-1, 0, 0, 1};

                    for (int i = 0; i < 4; ++i)
                    {
                        const int nx = x + dx[i];
                        const int ny = y + dy[i];

                        if (nx >= 0 && nx < W && ny >= 0 && ny < H)
                        {
                            if (distMap[idxOf(nx, ny, W)] == level - 1)
                            {
                                isBoundary = true;
                                break;
                            }
                        }
                        else if (level == 1)
                        {
                            // Edge pixels at level 1 are boundaries
                            isBoundary = true;
                            break;
                        }
                    }

                    if (isBoundary)
                    {
                        boundary[idx] = 255;
                    }
                }
            }
        }
    }

    // Trace connected boundary pixels into a single ordered path
    void traceBoundaryPaths(const std::vector<uint8_t> &boundary, int W, int H,
                           float pixel_size_mm, PathSet &out)
    {
        std::vector<bool> visited(static_cast<size_t>(W * H), false);

        // 8-connected neighbors
        const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
        const int dy8[8] = {0, -1, -1, -1, 0, 1, 1, 1};

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const size_t idx = idxOf(x, y, W);
                if (boundary[idx] == 255 && !visited[idx])
                {
                    Path path;
                    path.closed = false;

                    // Chain following: start at this pixel and follow the boundary
                    int cx = x, cy = y;
                    visited[idxOf(cx, cy, W)] = true;
                    path.points.emplace_back(
                        static_cast<float>(cx) * pixel_size_mm,
                        static_cast<float>(cy) * pixel_size_mm
                    );

                    bool foundNext = true;
                    while (foundNext)
                    {
                        foundNext = false;

                        // Try to find next connected boundary pixel
                        for (int i = 0; i < 8; ++i)
                        {
                            const int nx = cx + dx8[i];
                            const int ny = cy + dy8[i];

                            if (nx >= 0 && nx < W && ny >= 0 && ny < H)
                            {
                                const size_t nidx = idxOf(nx, ny, W);
                                if (boundary[nidx] == 255 && !visited[nidx])
                                {
                                    visited[nidx] = true;
                                    cx = nx;
                                    cy = ny;
                                    path.points.emplace_back(
                                        static_cast<float>(cx) * pixel_size_mm,
                                        static_cast<float>(cy) * pixel_size_mm
                                    );
                                    foundNext = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (path.points.size() > 1)
                    {
                        out.paths.push_back(std::move(path));
                    }
                }
            }
        }
    }
}

void ConcentricOutlineFilter::applyTyped(const Bitmap &in, PathSet &out) const
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

    const int threshold = clampi(static_cast<int>(std::lround(m_parameters.at("threshold").value)), 0, 255);
    const int spacing = std::max(1, static_cast<int>(std::lround(m_parameters.at("spacing_px").value)));

    // Binarize input: 0 = background (light), 255 = foreground (dark to be filled)
    std::vector<uint8_t> binary(static_cast<size_t>(W * H), 0);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const uint8_t v = in.pixels[idxOf(x, y, W)];
            // Dark pixels (<=threshold) are foreground (255), light pixels are background (0)
            binary[idxOf(x, y, W)] = (v <= threshold) ? 255 : 0;
        }
    }

    // Compute distance transform once - O(W*H)
    std::vector<int> distMap;
    computeDistanceTransform(binary, distMap, W, H);

    // Find maximum distance to determine how many levels we need
    int maxDist = 0;
    for (int d : distMap)
    {
        if (d < W + H) // Ignore INF values
            maxDist = std::max(maxDist, d);
    }

    // Extract and trace boundary at distance levels spaced by 'spacing' pixels
    std::vector<uint8_t> boundary;
    for (int level = 1; level <= maxDist; level += spacing)
    {
        setProgress(static_cast<float>(level) / static_cast<float>(maxDist));
        extractLevelBoundary(distMap, level, boundary, W, H);
        traceBoundaryPaths(boundary, W, H, in.pixel_size_mm, out);
    }
    setProgress(1.0f);

    out.computeAABB();
}
