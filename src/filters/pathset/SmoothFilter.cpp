#include "filters/pathset/SmoothFilter.h"

#include <vector>

static inline Vec2 lerp(const Vec2 &a, const Vec2 &b, float t)
{
    return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// One iteration of Chaikin's corner-cutting
static void chaikinOnce(const Path &in, Path &out)
{
    out.points.clear();
    out.closed = in.closed;

    const size_t n = in.points.size();
    if (n == 0)
        return;
    if (n == 1)
    {
        out.points.push_back(in.points[0]);
        return;
    }

    if (!in.closed)
    {
        // Preserve endpoints for open paths
        out.points.push_back(in.points[0]);
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const Vec2 &p0 = in.points[i];
            const Vec2 &p1 = in.points[i + 1];
            Vec2 q = lerp(p0, p1, 0.25f); // 0.75*p0 + 0.25*p1
            Vec2 r = lerp(p0, p1, 0.75f); // 0.25*p0 + 0.75*p1
            out.points.push_back(q);
            out.points.push_back(r);
        }
        out.points.push_back(in.points[n - 1]);
    }
    else
    {
        // Closed: process all edges with wrap-around
        for (size_t i = 0; i < n; ++i)
        {
            const Vec2 &p0 = in.points[i];
            const Vec2 &p1 = in.points[(i + 1) % n];
            Vec2 q = lerp(p0, p1, 0.25f);
            Vec2 r = lerp(p0, p1, 0.75f);
            out.points.push_back(q);
            out.points.push_back(r);
        }
    }
}

void SmoothFilter::applyTyped(const PathSet &in, PathSet &out) const
{
    out.color = in.color;
    out.paths.clear();
    out.paths.reserve(in.paths.size());

    int iters = static_cast<int>(m_parameters.at("iterations").value + 0.5f);
    if (iters < 0) iters = 0;
    if (iters > 50) iters = 50; // hard safety clamp

    for (const Path &p : in.paths)
    {
        if (p.points.size() < 2 || iters == 0)
        {
            out.paths.push_back(p);
            continue;
        }

        Path cur = p;
        Path tmp;
        for (int i = 0; i < iters; ++i)
        {
            chaikinOnce(cur, tmp);
            cur.points.swap(tmp.points);
            cur.closed = tmp.closed;
        }
        out.paths.push_back(std::move(cur));
    }

    out.computeAABB();
}


