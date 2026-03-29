#include "filters/bitmap/VoronoiStipplingFilter.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <thread>
#include <numeric>

namespace {

    // Pre-computed unit circle table for circle generation
    struct CircleLUT
    {
        static constexpr int kMaxSegments = 10;
        float cosTable[kMaxSegments];
        float sinTable[kMaxSegments];
        int segments = 0;

        void build(int n)
        {
            segments = n;
            for (int i = 0; i < n; ++i)
            {
                float angle = (static_cast<float>(i) / static_cast<float>(n)) * 2.0f * static_cast<float>(M_PI);
                cosTable[i] = std::cos(angle);
                sinTable[i] = std::sin(angle);
            }
        }

        Path makeCircle(Vec2 center, float radius) const
        {
            Path circle;
            circle.closed = true;
            circle.points.resize(segments);
            for (int i = 0; i < segments; ++i)
            {
                circle.points[i] = Vec2(center.x + radius * cosTable[i],
                                        center.y + radius * sinTable[i]);
            }
            return circle;
        }
    };

    // Flat bucket grid — single allocation, no per-cell vectors
    struct FlatGrid
    {
        std::vector<int> cellStart; // size = numCells + 1, prefix-sum offsets
        std::vector<int> pointIds;  // size = numPoints, sorted by cell
        int gridW = 0;
        int gridH = 0;
        float cellSize = 0.0f;

        void build(const std::vector<Vec2> &points, int numPoints, float cs, int gw, int gh)
        {
            gridW = gw;
            gridH = gh;
            cellSize = cs;
            const int numCells = gw * gh;

            // Count points per cell
            cellStart.assign(numCells + 1, 0);
            for (int i = 0; i < numPoints; ++i)
            {
                int gx = std::min(gw - 1, std::max(0, static_cast<int>(points[i].x / cs)));
                int gy = std::min(gh - 1, std::max(0, static_cast<int>(points[i].y / cs)));
                cellStart[gy * gw + gx + 1]++;
            }

            // Prefix sum
            for (int i = 1; i <= numCells; ++i)
                cellStart[i] += cellStart[i - 1];

            // Scatter points into sorted order
            pointIds.resize(numPoints);
            std::vector<int> writePos(cellStart.begin(), cellStart.begin() + numCells);
            for (int i = 0; i < numPoints; ++i)
            {
                int gx = std::min(gw - 1, std::max(0, static_cast<int>(points[i].x / cs)));
                int gy = std::min(gh - 1, std::max(0, static_cast<int>(points[i].y / cs)));
                int cell = gy * gw + gx;
                pointIds[writePos[cell]++] = i;
            }
        }

        // Iterate points in cell (gx, gy). Returns pointer range [begin, end).
        const int* cellBegin(int gx, int gy) const
        {
            int cell = gy * gridW + gx;
            return pointIds.data() + cellStart[cell];
        }
        const int* cellEnd(int gx, int gy) const
        {
            int cell = gy * gridW + gx;
            return pointIds.data() + cellStart[cell + 1];
        }
    };

    // Pre-compute intensity LUT: avoids per-pixel division and inversion
    void buildIntensityLUT(float lut[256])
    {
        for (int i = 0; i < 256; ++i)
            lut[i] = 1.0f - (static_cast<float>(i) / 255.0f);
    }

} // namespace

void VoronoiStipplingFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
    out.paths.clear();
    out.color = Color(0.0f, 0.0f, 0.0f, 1.0f);

    if (in.width_px <= 0 || in.height_px <= 0 || in.pixels.empty())
    {
        out.computeAABB();
        return;
    }

    const int numPoints = static_cast<int>(m_parameters.at("num_points").value);
    const int maxIterations = static_cast<int>(m_parameters.at("iterations").value);
    const float circleRadius = m_parameters.at("circle_radius").value;
    const float sizeVariation = m_parameters.at("size_variation").value;
    const int circleSegments = static_cast<int>(m_parameters.at("circle_segments").value);

    const int width = static_cast<int>(in.width_px);
    const int height = static_cast<int>(in.height_px);
    const float pxMm = in.pixel_size_mm;

    // Pre-compute intensity lookup table
    float intensityLUT[256];
    buildIntensityLUT(intensityLUT);

    // Initialize random number generator
    std::mt19937 rng(42); // Fixed seed for reproducibility

    // Build cumulative distribution for weighted sampling
    std::vector<float> cumulativeWeights;
    cumulativeWeights.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

    float totalWeight = 0.0f;
    for (int y = 0; y < height; ++y)
    {
        const uint8_t *row = in.pixels.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x)
        {
            float intensity = intensityLUT[row[x]];
            totalWeight += intensity + 0.01f;
            cumulativeWeights.push_back(totalWeight);
        }
    }

    if (totalWeight <= 0.0f || numPoints <= 0)
    {
        out.computeAABB();
        return;
    }

    // Initialize points using weighted random sampling
    std::vector<Vec2> points;
    points.reserve(numPoints);

    std::uniform_real_distribution<float> dist(0.0f, totalWeight);
    std::uniform_real_distribution<float> offsetDist(-0.5f, 0.5f);

    for (int i = 0; i < numPoints; ++i)
    {
        float sample = dist(rng);
        auto it = std::lower_bound(cumulativeWeights.begin(), cumulativeWeights.end(), sample);
        size_t idx = static_cast<size_t>(std::distance(cumulativeWeights.begin(), it));

        int px = static_cast<int>(idx % width);
        int py = static_cast<int>(idx / width);

        float fx = (static_cast<float>(px) + 0.5f + offsetDist(rng)) * pxMm;
        float fy = (static_cast<float>(py) + 0.5f + offsetDist(rng)) * pxMm;

        points.emplace_back(fx, fy);
    }

    // Compute grid constants once (they don't change between iterations)
    const float avgSpacing = std::sqrt((static_cast<float>(width) * pxMm *
                                        static_cast<float>(height) * pxMm) /
                                        static_cast<float>(numPoints));
    const float cellSize = avgSpacing * 2.0f;
    const int gridWidth = std::max(1, static_cast<int>(std::ceil(width * pxMm / cellSize)));
    const int gridHeight = std::max(1, static_cast<int>(std::ceil(height * pxMm / cellSize)));
    const float invCellSize = 1.0f / cellSize;

    // Adaptive subsampling based on point density relative to image size
    const float pixelsPerPoint = static_cast<float>(width * height) / static_cast<float>(numPoints);
    const int subsample = std::max(1, std::min(4, static_cast<int>(std::sqrt(pixelsPerPoint) * 0.1f)));

    // Thread count
    const int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));

    // Lloyd's relaxation iterations
    std::vector<float> pointWeights(numPoints, 0.0f);
    FlatGrid grid;

    // Per-thread accumulation buffers (allocated once, reused across iterations)
    // Layout: [thread0_newPoints... thread0_weights... thread1_newPoints... ...]
    std::vector<Vec2> allNewPoints(static_cast<size_t>(numThreads) * numPoints);
    std::vector<float> allWeights(static_cast<size_t>(numThreads) * numPoints);

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        setProgress(static_cast<float>(iter) / static_cast<float>(maxIterations));
        // Build flat bucket grid
        grid.build(points, numPoints, cellSize, gridWidth, gridHeight);

        // Zero out per-thread buffers
        std::fill(allNewPoints.begin(), allNewPoints.end(), Vec2(0.0f, 0.0f));
        std::fill(allWeights.begin(), allWeights.end(), 0.0f);

        // Partition rows across threads
        auto workerFn = [&](int threadIdx, int yStart, int yEnd)
        {
            Vec2 *localNewPoints = allNewPoints.data() + static_cast<size_t>(threadIdx) * numPoints;
            float *localWeights = allWeights.data() + static_cast<size_t>(threadIdx) * numPoints;

            for (int y = yStart; y < yEnd; y += subsample)
            {
                const uint8_t *row = in.pixels.data() + static_cast<size_t>(y) * width;
                float pixelY = (static_cast<float>(y) + 0.5f) * pxMm;
                int gy = std::min(gridHeight - 1, std::max(0, static_cast<int>(pixelY * invCellSize)));

                int gyMin = std::max(0, gy - 1);
                int gyMax = std::min(gridHeight - 1, gy + 1);

                for (int x = 0; x < width; x += subsample)
                {
                    float intensity = intensityLUT[row[x]];
                    if (intensity < 0.01f) continue;

                    float pixelX = (static_cast<float>(x) + 0.5f) * pxMm;
                    int gx = std::min(gridWidth - 1, std::max(0, static_cast<int>(pixelX * invCellSize)));

                    int gxMin = std::max(0, gx - 1);
                    int gxMax = std::min(gridWidth - 1, gx + 1);

                    // Search 3x3 neighborhood for closest point
                    int closestIdx = -1;
                    float closestDist = 1e20f;

                    for (int cgy = gyMin; cgy <= gyMax; ++cgy)
                    {
                        for (int cgx = gxMin; cgx <= gxMax; ++cgx)
                        {
                            const int *begin = grid.cellBegin(cgx, cgy);
                            const int *end = grid.cellEnd(cgx, cgy);

                            for (const int *it = begin; it != end; ++it)
                            {
                                int i = *it;
                                float dx = pixelX - points[i].x;
                                float dy = pixelY - points[i].y;
                                float d2 = dx * dx + dy * dy;

                                if (d2 < closestDist)
                                {
                                    closestDist = d2;
                                    closestIdx = i;
                                }
                            }
                        }
                    }

                    if (closestIdx < 0) continue;

                    localNewPoints[closestIdx].x += pixelX * intensity;
                    localNewPoints[closestIdx].y += pixelY * intensity;
                    localWeights[closestIdx] += intensity;
                }
            }
        };

        // Launch threads
        std::vector<std::thread> threads;
        threads.reserve(numThreads);

        int rowsPerThread = height / numThreads;
        for (int t = 0; t < numThreads; ++t)
        {
            int yStart = t * rowsPerThread;
            int yEnd = (t == numThreads - 1) ? height : (t + 1) * rowsPerThread;
            threads.emplace_back(workerFn, t, yStart, yEnd);
        }

        for (auto &th : threads)
            th.join();

        // Merge per-thread results and update positions
        for (int i = 0; i < numPoints; ++i)
        {
            float totalW = 0.0f;
            float sumX = 0.0f;
            float sumY = 0.0f;

            for (int t = 0; t < numThreads; ++t)
            {
                size_t off = static_cast<size_t>(t) * numPoints + i;
                sumX += allNewPoints[off].x;
                sumY += allNewPoints[off].y;
                totalW += allWeights[off];
            }

            if (totalW > 0.0f)
            {
                points[i] = Vec2(sumX / totalW, sumY / totalW);
            }
            pointWeights[i] = totalW;
        }
    }

    // Normalize weights for circle sizing
    float maxWeight = *std::max_element(pointWeights.begin(), pointWeights.end());
    if (maxWeight < 1.0f) maxWeight = 1.0f;

    // Pre-compute circle LUT
    CircleLUT circleLut;
    circleLut.build(circleSegments);

    // Generate final output
    setProgress(1.0f);
    out.paths.reserve(numPoints);
    for (int i = 0; i < numPoints; ++i)
    {
        float normalizedWeight = pointWeights[i] / maxWeight;
        float variableSize = circleRadius * (0.3f + normalizedWeight * 1.7f);
        float finalRadius = circleRadius * (1.0f - sizeVariation) + variableSize * sizeVariation;

        out.paths.push_back(circleLut.makeCircle(points[i], finalRadius));
    }
    out.computeAABB();
}
