#include "filters/pathset/SimplifyFilter.h"

void SimplifyFilter::applyTyped(const PathSet &in, PathSet &out) const
{
    out.color = in.color;
    out.paths.clear();
    out.paths.reserve(in.paths.size());

    // Very naive decimation based on tolerance: keep every Nth point
    int step = 1;
    const float tol = m_parameters.at("toleranceMm").value;
    if (tol > 0.0f)
    {
        // Map tolerance mm to an integer step (clamped to at least 1)
        step = static_cast<int>(tol);
        if (step < 1) step = 1;
    }

    for (const auto &p : in.paths)
    {
        Path sp;
        sp.closed = p.closed;
        sp.points.reserve(p.points.size());
        for (size_t i = 0; i < p.points.size(); i += static_cast<size_t>(step))
        {
            sp.points.push_back(p.points[i]);
        }
        // Ensure last point is included if closed or if step skipped the end
        if (!p.points.empty())
        {
            if (sp.points.empty() || sp.points.back().x != p.points.back().x || sp.points.back().y != p.points.back().y)
                sp.points.push_back(p.points.back());
        }
        out.paths.push_back(std::move(sp));
    }

    out.computeAABB();
}


