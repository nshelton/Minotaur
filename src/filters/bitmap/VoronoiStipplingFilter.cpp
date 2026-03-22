#include "filters/bitmap/VoronoiStipplingFilter.h"
#include <random>
#include <cmath>
#include <algorithm>

namespace {
    // Generate a circle path with given center and radius
    Path makeCircle(Vec2 center, float radius, int segments = 5)
    {
        Path circle;
        circle.closed = true;

        for (int i = 0; i < segments; ++i)
        {
            float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * static_cast<float>(M_PI);
            float x = center.x + radius * std::cos(angle);
            float y = center.y + radius * std::sin(angle);
            circle.points.emplace_back(x, y);
        }

        return circle;
    }

    // Convert pixel coordinates to mm space
    Vec2 pixelToMm(int px, int py, float pixel_size_mm)
    {
        return Vec2(
            (static_cast<float>(px) + 0.5f) * pixel_size_mm,
            (static_cast<float>(py) + 0.5f) * pixel_size_mm
        );
    }

    // Get pixel intensity (normalized 0-1, inverted so dark = high weight)
    float getIntensity(const Bitmap &bitmap, int x, int y)
    {
        if (x < 0 || x >= static_cast<int>(bitmap.width_px) ||
            y < 0 || y >= static_cast<int>(bitmap.height_px))
            return 0.0f;

        size_t idx = static_cast<size_t>(y) * bitmap.width_px + static_cast<size_t>(x);
        uint8_t val = bitmap.pixels[idx];
        // Invert: darker pixels = higher intensity for stippling
        return 1.0f - (static_cast<float>(val) / 255.0f);
    }
}

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

    // Initialize random number generator
    std::mt19937 rng(42); // Fixed seed for reproducibility

    // Build cumulative distribution for weighted sampling
    std::vector<float> cumulativeWeights;
    cumulativeWeights.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

    float totalWeight = 0.0f;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float intensity = getIntensity(in, x, y);
            totalWeight += intensity + 0.01f; // Add small epsilon to avoid zero weights
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

    for (int i = 0; i < numPoints; ++i)
    {
        float sample = dist(rng);

        // Binary search in cumulative weights
        auto it = std::lower_bound(cumulativeWeights.begin(), cumulativeWeights.end(), sample);
        size_t idx = std::distance(cumulativeWeights.begin(), it);

        int px = static_cast<int>(idx % width);
        int py = static_cast<int>(idx / width);

        // Add small random offset within pixel
        std::uniform_real_distribution<float> offsetDist(-0.5f, 0.5f);
        float offsetX = offsetDist(rng) * in.pixel_size_mm;
        float offsetY = offsetDist(rng) * in.pixel_size_mm;

        Vec2 pos = pixelToMm(px, py, in.pixel_size_mm);
        pos.x += offsetX;
        pos.y += offsetY;

        points.push_back(pos);
    }

    // Lloyd's relaxation iterations
    std::vector<float> pointWeights(numPoints, 0.0f); // Track weights for circle sizing

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        // Build spatial grid for faster nearest-neighbor queries
        // Grid cell size: roughly based on average point spacing
        const float avgSpacing = std::sqrt((static_cast<float>(width) * in.pixel_size_mm *
                                           static_cast<float>(height) * in.pixel_size_mm) /
                                           static_cast<float>(numPoints));
        const float cellSize = avgSpacing * 2.0f; // 2x average spacing
        const int gridWidth = std::max(1, static_cast<int>(std::ceil(width * in.pixel_size_mm / cellSize)));
        const int gridHeight = std::max(1, static_cast<int>(std::ceil(height * in.pixel_size_mm / cellSize)));

        std::vector<std::vector<int>> grid(gridWidth * gridHeight);

        // Assign points to grid cells
        for (int i = 0; i < numPoints; ++i)
        {
            int gx = std::min(gridWidth - 1, std::max(0, static_cast<int>(points[i].x / cellSize)));
            int gy = std::min(gridHeight - 1, std::max(0, static_cast<int>(points[i].y / cellSize)));
            grid[gy * gridWidth + gx].push_back(i);
        }

        // Compute weighted centroids for each point's Voronoi cell
        std::vector<Vec2> newPoints(numPoints, Vec2(0.0f, 0.0f));
        std::vector<float> weights(numPoints, 0.0f);

        // Subsample pixels for faster computation (check every 2nd or 3rd pixel)
        const int subsample = std::max(1, std::min(3, width / 200));

        // For each pixel (subsampled), find closest point and accumulate to its centroid
        for (int y = 0; y < height; y += subsample)
        {
            for (int x = 0; x < width; x += subsample)
            {
                Vec2 pixelPos = pixelToMm(x, y, in.pixel_size_mm);
                float intensity = getIntensity(in, x, y);

                if (intensity < 0.01f) continue; // Skip nearly white pixels

                // Find grid cell for this pixel
                int gx = std::min(gridWidth - 1, std::max(0, static_cast<int>(pixelPos.x / cellSize)));
                int gy = std::min(gridHeight - 1, std::max(0, static_cast<int>(pixelPos.y / cellSize)));

                // Search in 3x3 neighborhood of grid cells
                int closestIdx = -1;
                float closestDist = 1e20f;

                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int cgx = gx + dx;
                        int cgy = gy + dy;

                        if (cgx < 0 || cgx >= gridWidth || cgy < 0 || cgy >= gridHeight)
                            continue;

                        const auto& cellPoints = grid[cgy * gridWidth + cgx];

                        for (int i : cellPoints)
                        {
                            Vec2 diff = pixelPos - points[i];
                            float dist = diff.x * diff.x + diff.y * diff.y;

                            if (dist < closestDist)
                            {
                                closestDist = dist;
                                closestIdx = i;
                            }
                        }
                    }
                }

                if (closestIdx < 0) continue; // No points in neighborhood, skip pixel

                // Accumulate weighted centroid
                newPoints[closestIdx] += pixelPos * intensity;
                weights[closestIdx] += intensity;
            }
        }

        // Update point positions to weighted centroids
        for (int i = 0; i < numPoints; ++i)
        {
            if (weights[i] > 0.0f)
            {
                points[i] = newPoints[i] / weights[i];
            }
            // If a point has no weight, keep its current position
        }

        // Store weights from last iteration for circle sizing
        pointWeights = weights;
    }

    // Normalize weights for circle sizing
    float maxWeight = 1.0f;
    for (int i = 0; i < numPoints; ++i)
    {
        if (pointWeights[i] > maxWeight)
            maxWeight = pointWeights[i];
    }

    // Generate final output with variable-sized circles
    out.paths.clear();
    for (int i = 0; i < numPoints; ++i)
    {
        // Compute circle size based on local darkness (weight)
        float normalizedWeight = (maxWeight > 0.0f) ? (pointWeights[i] / maxWeight) : 0.0f;

        // Interpolate between fixed size (sizeVariation=0) and variable size (sizeVariation=1)
        // Higher weight (darker) = bigger circles
        float variableSize = circleRadius * (0.3f + normalizedWeight * 1.7f); // Range: 0.3x to 2x base radius
        float finalRadius = circleRadius * (1.0f - sizeVariation) + variableSize * sizeVariation;

        out.paths.push_back(makeCircle(points[i], finalRadius, circleSegments));
    }
    out.computeAABB();
}
