#include "filters/bitmap/HatchV2Filter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    static inline int clampi(int v, int lo, int hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }

    // A single dark run on one scan line.
    // t0/t1 are the run's projection onto the hatch direction (pixel units),
    // shared across all rows so overlaps between adjacent rows are directly
    // comparable. a/b are the mm-space endpoints at t0 (low) and t1 (high).
    struct Seg
    {
        float t0{0.0f};
        float t1{0.0f};
        Vec2 a;
        Vec2 b;
        std::vector<int> down; // overlapping segments in the next row
        bool visited{false};
    };
}

void HatchV2Filter::applyTyped(const Bitmap &in, PathSet &out) const
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
    const float connectScale = std::max(1.0f, m_parameters.at("connect_scale").value);

    // Normalize angle to [0, 360)
    float ang = std::fmod(angleDeg, 360.0f);
    if (ang < 0.0f) ang += 360.0f;
    const float theta = ang * 0.017453292519943295f; // pi/180

    const float dx = std::cos(theta);
    const float dy = std::sin(theta);
    // Perpendicular unit (normal); scan lines are stepped along this.
    const float nx = -dy;
    const float ny = dx;

    const float mmPerPx = in.pixel_size_mm;
    const float rowSpacingMm = stepPx * mmPerPx;
    const float maxConnMm = connectScale * rowSpacingMm;
    const float maxConn2 = maxConnMm * maxConnMm;
    const float tolPx = 1.0f; // along-line overlap tolerance

    auto isDark = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        const uint8_t v = in.pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
        return v <= threshold;
    };

    // Project rectangle corners on the normal to get the scan range (pixel space)
    const float corners[4][2] = {{0.0f, 0.0f}, {static_cast<float>(width), 0.0f}, {static_cast<float>(width), static_cast<float>(height)}, {0.0f, static_cast<float>(height)}};
    float sMin = std::numeric_limits<float>::infinity();
    float sMax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 4; ++i)
    {
        const float s = corners[i][0] * nx + corners[i][1] * ny;
        if (s < sMin) sMin = s;
        if (s > sMax) sMax = s;
    }

    const float eps = 1e-6f;
    auto addIntersections = [&](float s) -> std::vector<Vec2> {
        std::vector<Vec2> pts;
        pts.reserve(4);
        if (std::fabs(ny) > eps)
        {
            const float y0 = s / ny;
            if (y0 >= 0.0f && y0 <= static_cast<float>(height)) pts.emplace_back(0.0f, y0);
            const float yW = (s - nx * static_cast<float>(width)) / ny;
            if (yW >= 0.0f && yW <= static_cast<float>(height)) pts.emplace_back(static_cast<float>(width), yW);
        }
        if (std::fabs(nx) > eps)
        {
            const float x0 = s / nx;
            if (x0 >= 0.0f && x0 <= static_cast<float>(width)) pts.emplace_back(x0, 0.0f);
            const float xH = (s - ny * static_cast<float>(height)) / nx;
            if (xH >= 0.0f && xH <= static_cast<float>(width)) pts.emplace_back(xH, static_cast<float>(height));
        }
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

    std::vector<Seg> segs;             // all runs across all rows
    std::vector<std::vector<int>> rowSegs; // per-row indices into segs, sorted by t0

    // Sample one scan line and append its dark runs (as Segs) to the given row.
    auto emitRow = [&](const Vec2 &a, const Vec2 &b, std::vector<int> &rowOut) {
        const float L = std::hypot(b.x - a.x, b.y - a.y);
        if (L < 1.0f) return;
        const int samples = std::max(2, static_cast<int>(std::ceil(L)));

        auto pushRun = [&](int iStart, int iEnd) {
            const float f0 = static_cast<float>(iStart) / static_cast<float>(samples - 1);
            const float f1 = static_cast<float>(iEnd) / static_cast<float>(samples - 1);
            const float x0 = a.x + (b.x - a.x) * f0;
            const float y0 = a.y + (b.y - a.y) * f0;
            const float x1 = a.x + (b.x - a.x) * f1;
            const float y1 = a.y + (b.y - a.y) * f1;
            const float ta = x0 * dx + y0 * dy; // projection onto hatch direction (px)
            const float tb = x1 * dx + y1 * dy;
            const Vec2 pa(x0 * mmPerPx, y0 * mmPerPx);
            const Vec2 pb(x1 * mmPerPx, y1 * mmPerPx);
            Seg seg;
            if (ta <= tb) { seg.t0 = ta; seg.t1 = tb; seg.a = pa; seg.b = pb; }
            else          { seg.t0 = tb; seg.t1 = ta; seg.a = pb; seg.b = pa; }
            rowOut.push_back(static_cast<int>(segs.size()));
            segs.push_back(std::move(seg));
        };

        int runStart = -1;
        for (int i = 0; i < samples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
            const int xi = clampi(static_cast<int>(std::floor(a.x + (b.x - a.x) * t)), 0, width - 1);
            const int yi = clampi(static_cast<int>(std::floor(a.y + (b.y - a.y) * t)), 0, height - 1);
            if (isDark(xi, yi))
            {
                if (runStart < 0) runStart = i;
            }
            else if (runStart >= 0)
            {
                pushRun(runStart, i - 1);
                runStart = -1;
            }
        }
        if (runStart >= 0)
            pushRun(runStart, samples - 1);
    };

    // Sweep across the image with parallel lines spaced by stepPx along the normal.
    const int sStart = static_cast<int>(std::floor(sMin));
    const int sEnd = static_cast<int>(std::ceil(sMax));
    const float sRange = static_cast<float>(sEnd - sStart);
    for (float s = static_cast<float>(sStart); s <= static_cast<float>(sEnd); s += stepPx)
    {
        if (sRange > 0.0f)
            setProgress(0.5f * (s - static_cast<float>(sStart)) / sRange);
        rowSegs.emplace_back();
        std::vector<int> &row = rowSegs.back();
        const std::vector<Vec2> ints = addIntersections(s);
        if (ints.size() >= 2)
            emitRow(ints[0], ints[1], row);
        std::sort(row.begin(), row.end(), [&](int A, int B) { return segs[A].t0 < segs[B].t0; });
    }

    // Build downward adjacency: a run overlaps a run in the next row when their
    // [t0,t1] intervals overlap (within tolerance). Interval-join sweep, O(n+edges).
    for (size_t r = 0; r + 1 < rowSegs.size(); ++r)
    {
        const std::vector<int> &R0 = rowSegs[r];
        const std::vector<int> &R1 = rowSegs[r + 1];
        if (R0.empty() || R1.empty()) continue;
        int lo = 0;
        for (int ii = 0; ii < static_cast<int>(R0.size()); ++ii)
        {
            Seg &si = segs[R0[ii]];
            while (lo < static_cast<int>(R1.size()) && segs[R1[lo]].t1 < si.t0 - tolPx)
                ++lo;
            for (int jj = lo; jj < static_cast<int>(R1.size()); ++jj)
            {
                const Seg &sj = segs[R1[jj]];
                if (sj.t0 > si.t1 + tolPx) break;
                if (sj.t1 >= si.t0 - tolPx)
                    si.down.push_back(R1[jj]);
            }
        }
    }

    auto dist2 = [](const Vec2 &p, const Vec2 &q) -> float {
        const float ddx = p.x - q.x;
        const float ddy = p.y - q.y;
        return ddx * ddx + ddy * ddy;
    };

    // Serpentine traversal. Process rows top to bottom; each unvisited run starts
    // a stroke that descends row by row, entering each run at whichever end is
    // nearest the previous exit (this makes the direction alternate naturally).
    for (size_t r = 0; r < rowSegs.size(); ++r)
    {
        if (!rowSegs.empty())
            setProgress(0.5f + 0.5f * static_cast<float>(r) / static_cast<float>(rowSegs.size()));
        for (int idx : rowSegs[r])
        {
            if (segs[idx].visited) continue;

            Path path;
            path.closed = false;
            int cur = idx;
            bool enterLow = true; // first run: enter at low-t end (a), exit at high-t end (b)
            while (true)
            {
                Seg &s = segs[cur];
                s.visited = true;
                const Vec2 entry = enterLow ? s.a : s.b;
                const Vec2 exit = enterLow ? s.b : s.a;
                // Connector from the previous exit to this entry is implicit: the
                // entry point is simply appended after the previous exit point.
                path.points.push_back(entry);
                path.points.push_back(exit);

                int bestJ = -1;
                bool bestEnterLow = true;
                float bestD2 = std::numeric_limits<float>::infinity();
                for (int j : s.down)
                {
                    if (segs[j].visited) continue;
                    const float dA = dist2(exit, segs[j].a);
                    const float dB = dist2(exit, segs[j].b);
                    if (dA < bestD2) { bestD2 = dA; bestJ = j; bestEnterLow = true; }
                    if (dB < bestD2) { bestD2 = dB; bestJ = j; bestEnterLow = false; }
                }
                if (bestJ < 0 || bestD2 > maxConn2)
                    break;

                cur = bestJ;
                enterLow = bestEnterLow;
            }
            out.paths.push_back(std::move(path));
        }
    }

    setProgress(1.0f);
    out.computeAABB();
}
