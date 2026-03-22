#include "filters/pathset/SimplifyFilter.h"

#include <algorithm>
#include <vector>
#include <cmath>

static inline float perpendicularDistanceToSegment(const Vec2 &p, const Vec2 &a, const Vec2 &b)
{
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float wx = p.x - a.x;
    const float wy = p.y - a.y;
    const float len2 = vx * vx + vy * vy;
    if (len2 <= 1e-12f)
    {
        const float dx = p.x - a.x;
        const float dy = p.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    const float t = std::max(0.0f, std::min(1.0f, (wx * vx + wy * vy) / len2));
    const float projx = a.x + t * vx;
    const float projy = a.y + t * vy;
    const float dx = p.x - projx;
    const float dy = p.y - projy;
    return std::sqrt(dx * dx + dy * dy);
}

static void rdpRecursive(const std::vector<Vec2> &pts, size_t first, size_t last, float eps, std::vector<bool> &keep)
{
    if (last <= first + 1)
        return;

    size_t index = first;
    float maxDist = -1.0f;
    const Vec2 &a = pts[first];
    const Vec2 &b = pts[last];
    for (size_t i = first + 1; i < last; ++i)
    {
        float d = perpendicularDistanceToSegment(pts[i], a, b);
        if (d > maxDist)
        {
            maxDist = d;
            index = i;
        }
    }

    if (maxDist > eps)
    {
        keep[index] = true;
        rdpRecursive(pts, first, index, eps, keep);
        rdpRecursive(pts, index, last, eps, keep);
    }
}

static void rdpSimplifyOpen(const std::vector<Vec2> &inPts, float eps, std::vector<Vec2> &outPts)
{
    const size_t n = inPts.size();
    if (n == 0) { outPts.clear(); return; }
    if (n <= 2) { outPts = inPts; return; }

    std::vector<bool> keep(n, false);
    keep[0] = true;
    keep[n - 1] = true;
    rdpRecursive(inPts, 0, n - 1, eps, keep);

    outPts.clear();
    outPts.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) outPts.push_back(inPts[i]);
}

static void mergeColinear(const std::vector<Vec2> &inPts, bool closed, float eps, std::vector<Vec2> &outPts)
{
    const size_t n = inPts.size();
    if (n <= 2) { outPts = inPts; return; }

    outPts.clear();
    outPts.reserve(n);

    if (!closed)
    {
        outPts.push_back(inPts[0]);
        for (size_t i = 1; i + 1 < n; ++i)
        {
            const Vec2 &a = outPts.back();
            const Vec2 &b = inPts[i + 1];
            const Vec2 &m = inPts[i];
            float d = perpendicularDistanceToSegment(m, a, b);
            if (d > eps)
                outPts.push_back(m);
        }
        outPts.push_back(inPts[n - 1]);
    }
    else
    {
        outPts.assign(inPts.begin(), inPts.end());
        bool changed = true;
        while (changed && outPts.size() > 2)
        {
            changed = false;
            std::vector<Vec2> next;
            next.reserve(outPts.size());
            const size_t cn = outPts.size();
            for (size_t i = 0; i < cn; ++i)
            {
                const Vec2 &a = outPts[(i + cn - 1) % cn];
                const Vec2 &b = outPts[(i + 1) % cn];
                float d = perpendicularDistanceToSegment(outPts[i], a, b);
                if (d > eps)
                    next.push_back(outPts[i]);
                else
                    changed = true;
            }
            if (next.size() >= 3)
                outPts = std::move(next);
            else
                break;
        }
    }
}

static void rdpSimplifyClosed(const std::vector<Vec2> &inPts, float eps, std::vector<Vec2> &outPts)
{
    const size_t n = inPts.size();
    if (n == 0) { outPts.clear(); return; }
    if (n <= 3) { outPts = inPts; return; }

    Vec2 c(0.0f, 0.0f);
    for (const auto &p : inPts) { c.x += p.x; c.y += p.y; }
    c.x /= static_cast<float>(n);
    c.y /= static_cast<float>(n);
    size_t start = 0;
    float maxd2 = -1.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const float dx = inPts[i].x - c.x;
        const float dy = inPts[i].y - c.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 > maxd2) { maxd2 = d2; start = i; }
    }

    std::vector<Vec2> seq;
    seq.reserve(n);
    for (size_t i = 0; i < n; ++i)
        seq.push_back(inPts[(start + i) % n]);

    std::vector<Vec2> tmp;
    rdpSimplifyOpen(seq, eps, tmp);

    std::vector<Vec2> merged;
    mergeColinear(tmp, true, eps, merged);
    outPts = (merged.size() < 3) ? tmp : std::move(merged);
}

static float computePathLengthMm(const Path &p)
{
    const size_t n = p.points.size();
    if (n < 2) return 0.0f;
    float len = 0.0f;
    for (size_t i = 1; i < n; ++i)
    {
        const float dx = p.points[i].x - p.points[i - 1].x;
        const float dy = p.points[i].y - p.points[i - 1].y;
        len += std::sqrt(dx * dx + dy * dy);
    }
    if (p.closed && n >= 2)
    {
        const float dx = p.points[0].x - p.points[n - 1].x;
        const float dy = p.points[0].y - p.points[n - 1].y;
        len += std::sqrt(dx * dx + dy * dy);
    }
    return len;
}

void SimplifyFilter::applyTyped(const PathSet &in, PathSet &out) const
{
    out.color = in.color;
    out.paths.clear();
    out.paths.reserve(in.paths.size());

    const float eps = std::max(0.0f, m_parameters.at("toleranceMm").value);
    const float minLen = std::max(0.0f, std::min(10.0f, m_parameters.at("minPathLengthMm").value));

    for (const auto &p : in.paths)
    {
        Path sp;
        sp.closed = p.closed;

        if (!p.closed)
        {
            if (p.points.size() <= 2)
            {
                sp = p;
            }
            else
            {
                std::vector<Vec2> simplified;
                rdpSimplifyOpen(p.points, eps, simplified);
                mergeColinear(simplified, false, eps, sp.points);
            }
        }
        else
        {
            rdpSimplifyClosed(p.points, eps, sp.points);
            sp.closed = true;
        }

        if ((!sp.closed && sp.points.size() < 2) || (sp.closed && sp.points.size() < 3))
            continue;

        // Remove paths shorter than threshold
        if (computePathLengthMm(sp) >= minLen)
        {
            out.paths.push_back(std::move(sp));
        }
    }

    out.computeAABB();
}

