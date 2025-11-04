#include "filters/bitmap/TraceBlobsFilter.h"

#include <vector>
#include <queue>
#include <cmath>
#include <cstdint>
#include <climits>
#include <unordered_set>

namespace {
    struct IVec2 { int x; int y; };

    // Non-recursive Ramer–Douglas–Peucker on Vec2 in mm space (stack-based to avoid deep recursion)
    static std::vector<Vec2> rdp(const std::vector<Vec2> &pts, float eps)
    {
        if (pts.size() <= 2 || eps <= 0.0f) return pts;
        const float epsSq = eps * eps;

        std::vector<char> keep(pts.size(), 0);
        keep.front() = 1;
        keep.back() = 1;

        struct Segment { size_t i0; size_t i1; };
        std::vector<Segment> stack;
        stack.push_back({0, pts.size() - 1});

        while (!stack.empty())
        {
            const Segment seg = stack.back();
            stack.pop_back();

            if (seg.i1 <= seg.i0 + 1) continue;

            const Vec2 &A = pts[seg.i0];
            const Vec2 &B = pts[seg.i1];
            const float dx = B.x - A.x;
            const float dy = B.y - A.y;
            const float denom = (dx * dx + dy * dy) > 1e-20f ? (dx * dx + dy * dy) : 1e-12f;

            float maxDistSq = -1.0f;
            size_t idx = seg.i0;
            for (size_t i = seg.i0 + 1; i < seg.i1; ++i)
            {
                const Vec2 &P = pts[i];
                const float u = ((P.x - A.x) * dx + (P.y - A.y) * dy) / denom;
                const float px = A.x + u * dx;
                const float py = A.y + u * dy;
                const float ddx = px - P.x;
                const float ddy = py - P.y;
                const float d2 = ddx * ddx + ddy * ddy;
                if (d2 > maxDistSq) { maxDistSq = d2; idx = i; }
            }

            if (maxDistSq > epsSq)
            {
                keep[idx] = 1;
                stack.push_back({seg.i0, idx});
                stack.push_back({idx, seg.i1});
            }
        }

        std::vector<Vec2> out;
        out.reserve(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (keep[i]) out.push_back(pts[i]);
        }
        return out;
    }
}

// Algorithm overview
// 1) Binarize the bitmap (fixed threshold at 128) to a black/white mask
// 2) 4-connected component labeling via BFS over black pixels to get each blob
// 3) For each blob, perform Moore-neighborhood boundary tracing to extract the
//    outer contour as a closed polyline in page mm space. Use an edge-visited
//    set to avoid multi-lap duplicates and a step cap to avoid hangs.
// 4) Optionally trace inner contours (holes) by flood-filling background cells
//    connected to the component bounding box edges (outside), then tracing any
//    remaining background regions (interior islands) exactly like the outer
//    contour. Each hole becomes its own closed polyline.
// 5) Optionally simplify using Ramer–Douglas–Peucker with tolerance in pixels
//    converted to mm.
void TraceBlobsFilter::applyTyped(const Bitmap &in, PathSet &out) const
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

    const auto idxOf = [W](int x, int y) { return y * W + x; };

    // Binary mask: black = value < 128
    std::vector<uint8_t> black(static_cast<size_t>(W * H), 0);
    for (int y = 0; y < H; ++y)
    {
        const int row = y * W;
        for (int x = 0; x < W; ++x)
        {
            const size_t i = static_cast<size_t>(row + x);
            black[i] = (in.pixels[i] < 128) ? 1 : 0;
        }
    }

    std::vector<uint8_t> visited(static_cast<size_t>(W * H), 0);
    const IVec2 n4[4] = {{1,0},{-1,0},{0,1},{0,-1}};
    const IVec2 n8[8] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};

    const int turdSize = static_cast<int>(std::round(m_parameters.at("turdSizePx").value));
    const float tolPx = m_parameters.at("tolerancePx").value;
    const float epsMm = std::max(0.0f, tolPx) * in.pixel_size_mm;
    const bool traceHoles = m_parameters.at("traceHoles").value > 0.5f;

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const int idx = idxOf(x, y);
            if (visited[static_cast<size_t>(idx)]) continue;
            visited[static_cast<size_t>(idx)] = 1;
            if (!black[static_cast<size_t>(idx)]) continue;

            // BFS to collect component
            std::vector<int> q;
            std::vector<int> comp;
            q.reserve(256);
            comp.reserve(256);
            q.push_back(idx);
            comp.push_back(idx);
            int minX = x, minY = y, maxX = x, maxY = y;
            while (!q.empty())
            {
                int p = q.back(); q.pop_back();
                const int py = p / W;
                const int px = p % W;
                for (const auto &d : n4)
                {
                    const int nx = px + d.x;
                    const int ny = py + d.y;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const int ni = idxOf(nx, ny);
                    if (visited[static_cast<size_t>(ni)]) continue;
                    visited[static_cast<size_t>(ni)] = 1;
                    if (black[static_cast<size_t>(ni)])
                    {
                        q.push_back(ni);
                        comp.push_back(ni);
                        if (nx < minX) minX = nx;
                        if (nx > maxX) maxX = nx;
                        if (ny < minY) minY = ny;
                        if (ny > maxY) maxY = ny;
                    }
                }
            }

            if (static_cast<int>(comp.size()) < turdSize) continue;

            const int bw = maxX - minX + 1;
            const int bh = maxY - minY + 1;
            if (bw <= 0 || bh <= 0) continue;
            std::vector<uint8_t> inComp(static_cast<size_t>(bw * bh), 0);
            for (int p : comp)
            {
                const int py = p / W; const int px = p % W;
                inComp[static_cast<size_t>((py - minY) * bw + (px - minX))] = 1;
            }

            // Find boundary start: top-most then left-most with a background 4-neighbor
            int startX = -1, startY = -1;
            for (int p : comp)
            {
                const int py = p / W; const int px = p % W;
                const int lx = px - minX; const int ly = py - minY;
                bool boundary = false;
                for (const auto &d : n4)
                {
                    const int nx = lx + d.x; const int ny = ly + d.y;
                    if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) { boundary = true; break; }
                    if (!inComp[static_cast<size_t>(ny * bw + nx)]) { boundary = true; break; }
                }
                if (boundary)
                {
                    if (startY == -1 || py < startY || (py == startY && px < startX))
                    { startX = px; startY = py; }
                }
            }
            if (startX == -1) continue;

            // Moore-neighbor tracing with edge-visited guard to prevent multi-lap duplicates
            int cx = startX - minX; int cy = startY - minY;
            int bx = startX - 1; int by = startY;
            int lbx = bx - minX; int lby = by - minY;
            const int sx = cx, sy = cy, sbx = lbx, sby = lby;

            auto dirIndex = [&](int px, int py, int qx, int qy) -> int {
                const int dx = qx - px; const int dy = qy - py;
                for (int k = 0; k < 8; ++k) if (n8[k].x == dx && n8[k].y == dy) return k;
                return 0;
            };

            std::vector<Vec2> path;
            path.reserve(static_cast<size_t>(bw + bh));

            // Cap tracing steps to a multiple of component size to avoid hangs on huge images
            const size_t stepCap = comp.size() * 8ull;
            const int maxSteps = static_cast<int>(std::min<size_t>(static_cast<size_t>(INT_MAX / 2), stepCap));
            int steps = 0;
            struct Edge { int x0,y0,x1,y1; };
            struct EdgeHash { size_t operator()(const Edge &e) const noexcept {
                // mix ints into size_t
                uint64_t k0 = (static_cast<uint64_t>(static_cast<uint32_t>(e.x0)) << 32) ^ static_cast<uint32_t>(e.y0);
                uint64_t k1 = (static_cast<uint64_t>(static_cast<uint32_t>(e.x1)) << 32) ^ static_cast<uint32_t>(e.y1);
                uint64_t h = k0 * 1469598103934665603ull ^ (k1 + 1099511628211ull);
                return static_cast<size_t>(h);
            }};
            struct EdgeEq { bool operator()(const Edge &a, const Edge &b) const noexcept { return a.x0==b.x0 && a.y0==b.y0 && a.x1==b.x1 && a.y1==b.y1; } };
            std::unordered_set<Edge, EdgeHash, EdgeEq> visitedEdges;
            while (steps++ < maxSteps)
            {
                const int k = dirIndex(cx, cy, lbx, lby);
                bool found = false;
                int nx = cx, ny = cy, nbx = lbx, nby = lby;
                for (int t = 1; t <= 8; ++t)
                {
                    const int j = (k + t) % 8;
                    const int qx = cx + n8[j].x; const int qy = cy + n8[j].y;
                    if (qx < 0 || qy < 0 || qx >= bw || qy >= bh) continue;
                    if (inComp[static_cast<size_t>(qy * bw + qx)])
                    {
                        const int jp = (j + 7) % 8;
                        nbx = cx + n8[jp].x; nby = cy + n8[jp].y;
                        nx = qx; ny = qy; found = true; break;
                    }
                }
                if (!found) break;

                // Edge-visited guard: if we're about to traverse an already seen directed edge, stop
                Edge e{cx, cy, nx, ny};
                if (visitedEdges.find(e) != visitedEdges.end())
                    break;
                visitedEdges.insert(e);

                // record current boundary point in mm page-local space at pixel center
                const float px_mm = (static_cast<float>(minX + cx) + 0.5f) * in.pixel_size_mm;
                const float py_mm = (static_cast<float>(minY + cy) + 0.5f) * in.pixel_size_mm;
                if (path.empty() || path.back().x != px_mm || path.back().y != py_mm)
                    path.emplace_back(px_mm, py_mm);

                // If next position returns to start cell, we completed a loop
                if (nx == sx && ny == sy)
                    break;

                cx = nx; cy = ny; lbx = nbx; lby = nby;
            }

            // Ensure closed
            if (path.size() > 1)
            {
                const Vec2 &f = path.front();
                const Vec2 &l = path.back();
                if (f.x != l.x || f.y != l.y) path.push_back(f);
            }

            std::vector<Vec2> simplified;
            if (epsMm > 0.0f && path.size() > 3)
                simplified = rdp(path, epsMm);
            else
                simplified = path;

            if (simplified.size() >= 3)
            {
                Path pp;
                pp.closed = true;
                pp.points = std::move(simplified);
                out.paths.push_back(std::move(pp));
            }

            // Optional: trace interior holes (enclosed background regions)
            if (traceHoles)
            {
                // Background mask inside this blob's bounding box
                std::vector<uint8_t> bg(static_cast<size_t>(bw * bh), 0);
                for (int yy = 0; yy < bh; ++yy)
                {
                    for (int xx = 0; xx < bw; ++xx)
                    {
                        const size_t ii = static_cast<size_t>(yy * bw + xx);
                        bg[ii] = inComp[ii] ? 0 : 1;
                    }
                }

                // Mark outside-connected background by flood fill from bbox border
                std::vector<uint8_t> outside(static_cast<size_t>(bw * bh), 0);
                std::vector<int> stk; stk.reserve(static_cast<size_t>(bw * 2 + bh * 2));
                auto push_if_bg = [&](int lx, int ly)
                {
                    if (lx < 0 || ly < 0 || lx >= bw || ly >= bh) return;
                    const int ii = ly * bw + lx;
                    if (bg[static_cast<size_t>(ii)] && !outside[static_cast<size_t>(ii)])
                    {
                        outside[static_cast<size_t>(ii)] = 1;
                        stk.push_back(ii);
                    }
                };
                for (int xx = 0; xx < bw; ++xx) { push_if_bg(xx, 0); push_if_bg(xx, bh - 1); }
                for (int yy = 0; yy < bh; ++yy) { push_if_bg(0, yy); push_if_bg(bw - 1, yy); }
                while (!stk.empty())
                {
                    int p = stk.back(); stk.pop_back();
                    const int py = p / bw; const int px = p % bw;
                    for (const auto &d : n4)
                    {
                        const int nx = px + d.x; const int ny = py + d.y;
                        if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
                        const int ni = ny * bw + nx;
                        if (bg[static_cast<size_t>(ni)] && !outside[static_cast<size_t>(ni)])
                        {
                            outside[static_cast<size_t>(ni)] = 1;
                            stk.push_back(ni);
                        }
                    }
                }

                // Interior background = background not connected to outside
                std::vector<uint8_t> interior(static_cast<size_t>(bw * bh), 0);
                for (int ii = 0; ii < bw * bh; ++ii)
                    interior[static_cast<size_t>(ii)] = (bg[static_cast<size_t>(ii)] && !outside[static_cast<size_t>(ii)]) ? 1 : 0;

                // Visit each interior white component and trace its boundary
                std::vector<uint8_t> visitedHole(static_cast<size_t>(bw * bh), 0);
                for (int ly = 0; ly < bh; ++ly)
                {
                    for (int lx = 0; lx < bw; ++lx)
                    {
                        const int si = ly * bw + lx;
                        if (!interior[static_cast<size_t>(si)] || visitedHole[static_cast<size_t>(si)]) continue;

                        // Collect this hole component (4-connected)
                        std::vector<int> hq; hq.reserve(128);
                        std::vector<int> holeComp; holeComp.reserve(128);
                        hq.push_back(si); visitedHole[static_cast<size_t>(si)] = 1;
                        while (!hq.empty())
                        {
                            int p = hq.back(); hq.pop_back();
                            holeComp.push_back(p);
                            const int py = p / bw; const int px = p % bw;
                            for (const auto &d : n4)
                            {
                                const int nx = px + d.x; const int ny = py + d.y;
                                if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
                                const int ni = ny * bw + nx;
                                if (interior[static_cast<size_t>(ni)] && !visitedHole[static_cast<size_t>(ni)])
                                {
                                    visitedHole[static_cast<size_t>(ni)] = 1;
                                    hq.push_back(ni);
                                }
                            }
                        }

                        if (static_cast<int>(holeComp.size()) < turdSize) continue;

                        // Build mask for this hole component
                        std::vector<uint8_t> inHole(static_cast<size_t>(bw * bh), 0);
                        for (int p : holeComp)
                            inHole[static_cast<size_t>(p)] = 1;

                        // Find boundary start for hole
                        int hstartX = -1, hstartY = -1;
                        for (int p : holeComp)
                        {
                            const int py = p / bw; const int px = p % bw;
                            bool boundary = false;
                            for (const auto &d : n4)
                            {
                                const int nx = px + d.x; const int ny = py + d.y;
                                if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) { boundary = true; break; }
                                if (!inHole[static_cast<size_t>(ny * bw + nx)]) { boundary = true; break; }
                            }
                            if (boundary)
                            {
                                if (hstartY == -1 || py < hstartY || (py == hstartY && px < hstartX))
                                { hstartX = px; hstartY = py; }
                            }
                        }
                        if (hstartX == -1) continue;

                        // Moore-neighbor tracing around the interior region
                        int cx2 = hstartX; int cy2 = hstartY;
                        int bx2 = hstartX - 1; int by2 = hstartY;
                        const int sx2 = cx2, sy2 = cy2;

                        auto dirIndex2 = [&](int px, int py, int qx, int qy) -> int {
                            const int dx = qx - px; const int dy = qy - py;
                            for (int k = 0; k < 8; ++k) if (n8[k].x == dx && n8[k].y == dy) return k;
                            return 0;
                        };

                        std::vector<Vec2> hpath; hpath.reserve(static_cast<size_t>(bw + bh));
                        const size_t stepCap2 = holeComp.size() * 8ull;
                        const int maxSteps2 = static_cast<int>(std::min<size_t>(static_cast<size_t>(INT_MAX / 2), stepCap2));
                        int steps2 = 0;
                        struct Edge { int x0,y0,x1,y1; };
                        struct EdgeHash { size_t operator()(const Edge &e) const noexcept {
                            uint64_t k0 = (static_cast<uint64_t>(static_cast<uint32_t>(e.x0)) << 32) ^ static_cast<uint32_t>(e.y0);
                            uint64_t k1 = (static_cast<uint64_t>(static_cast<uint32_t>(e.x1)) << 32) ^ static_cast<uint32_t>(e.y1);
                            uint64_t h = k0 * 1469598103934665603ull ^ (k1 + 1099511628211ull);
                            return static_cast<size_t>(h);
                        }};
                        struct EdgeEq { bool operator()(const Edge &a, const Edge &b) const noexcept { return a.x0==b.x0 && a.y0==b.y0 && a.x1==b.x1 && a.y1==b.y1; } };
                        std::unordered_set<Edge, EdgeHash, EdgeEq> visitedEdges2;
                        while (steps2++ < maxSteps2)
                        {
                            const int k = dirIndex2(cx2, cy2, bx2, by2);
                            bool found = false;
                            int nx = cx2, ny = cy2, nbx = bx2, nby = by2;
                            for (int t = 1; t <= 8; ++t)
                            {
                                const int j = (k + t) % 8;
                                const int qx = cx2 + n8[j].x; const int qy = cy2 + n8[j].y;
                                if (qx < 0 || qy < 0 || qx >= bw || qy >= bh) continue;
                                if (inHole[static_cast<size_t>(qy * bw + qx)])
                                {
                                    const int jp = (j + 7) % 8;
                                    nbx = cx2 + n8[jp].x; nby = cy2 + n8[jp].y;
                                    nx = qx; ny = qy; found = true; break;
                                }
                            }
                            if (!found) break;

                            Edge e{cx2, cy2, nx, ny};
                            if (visitedEdges2.find(e) != visitedEdges2.end())
                                break;
                            visitedEdges2.insert(e);

                            // pixel center in page mm
                            const float px_mm2 = (static_cast<float>(minX + cx2) + 0.5f) * in.pixel_size_mm;
                            const float py_mm2 = (static_cast<float>(minY + cy2) + 0.5f) * in.pixel_size_mm;
                            if (hpath.empty() || hpath.back().x != px_mm2 || hpath.back().y != py_mm2)
                                hpath.emplace_back(px_mm2, py_mm2);

                            if (nx == sx2 && ny == sy2)
                                break;

                            cx2 = nx; cy2 = ny; bx2 = nbx; by2 = nby;
                        }

                        if (hpath.size() > 1)
                        {
                            const Vec2 &f = hpath.front();
                            const Vec2 &l = hpath.back();
                            if (f.x != l.x || f.y != l.y) hpath.push_back(f);
                        }

                        std::vector<Vec2> hsimplified;
                        if (epsMm > 0.0f && hpath.size() > 3)
                            hsimplified = rdp(hpath, epsMm);
                        else
                            hsimplified = hpath;

                        if (hsimplified.size() >= 3)
                        {
                            Path hp; hp.closed = true; hp.points = std::move(hsimplified);
                            out.paths.push_back(std::move(hp));
                        }
                    }
                }
            }
        }
    }

    out.computeAABB();
}



