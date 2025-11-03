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
        }
    }

    out.computeAABB();
}



