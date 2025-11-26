#include "filters/pathset/RegularSubdivisionFilter.h"
#include <cmath>

void RegularSubdivisionFilter::applyTyped(const PathSet &in, PathSet &out) const
{
    out.paths.clear();
    out.color = in.color;

    const float spacing = m_parameters.at("spacing").value;

    if (spacing <= 0.0f)
    {
        out = in;
        out.computeAABB();
        return;
    }

    // Process each path
    for (const auto &path : in.paths)
    {
        if (path.points.empty())
            continue;

        Path newPath;
        newPath.closed = path.closed;

        if (path.points.size() == 1)
        {
            // Single point path - just copy it
            newPath.points.push_back(path.points[0]);
            out.paths.push_back(newPath);
            continue;
        }

        // Subdivide each segment
        for (size_t i = 0; i < path.points.size(); ++i)
        {
            const Vec2 &p0 = path.points[i];

            // Always add the current point
            newPath.points.push_back(p0);

            // Determine next point (handle closed paths)
            size_t nextIdx = i + 1;
            if (nextIdx >= path.points.size())
            {
                if (path.closed && path.points.size() > 1)
                {
                    // For closed paths, connect last point to first
                    nextIdx = 0;
                }
                else
                {
                    // Last point of open path - no subdivision needed
                    break;
                }
            }

            const Vec2 &p1 = path.points[nextIdx];

            // Calculate segment length
            Vec2 diff = p1 - p0;
            float segmentLength = std::sqrt(diff.x * diff.x + diff.y * diff.y);

            // Calculate how many subdivisions we need
            if (segmentLength > spacing)
            {
                int numSubdivisions = static_cast<int>(std::ceil(segmentLength / spacing));

                // Add intermediate points
                for (int j = 1; j < numSubdivisions; ++j)
                {
                    float t = static_cast<float>(j) / static_cast<float>(numSubdivisions);
                    Vec2 interpolated = p0 + diff * t;
                    newPath.points.push_back(interpolated);
                }
            }

            // For closed paths, stop before wrapping around to avoid duplicating first point
            if (path.closed && nextIdx == 0)
            {
                break;
            }
        }

        out.paths.push_back(newPath);
    }

    out.computeAABB();
}
