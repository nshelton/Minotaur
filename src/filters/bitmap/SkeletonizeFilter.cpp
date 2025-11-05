#include "filters/bitmap/SkeletonizeFilter.h"

#include <vector>
#include <cstdint>
#include <queue>
#include <cmath>
#include <chrono>

namespace {
	struct IVec2 { int x; int y; };

	static inline size_t idxOf(int x, int y, int W) { return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x); }

	// 8-neighborhood in clockwise order starting at north (p2..p9 in Zhang-Suen notation)
	static const IVec2 N8[8] = { {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1} };

	// Ramer–Douglas–Peucker simplification (non-recursive)
	static std::vector<Vec2> rdp(const std::vector<Vec2> &pts, float eps)
	{
		if (pts.size() <= 2 || eps <= 0.0f) return pts;
		const float epsSq = eps * eps;
		std::vector<char> keep(pts.size(), 0);
		keep.front() = 1; keep.back() = 1;
		struct Segment { size_t i0; size_t i1; };
		std::vector<Segment> stack;
		stack.push_back({0, pts.size() - 1});
		while (!stack.empty())
		{
			Segment seg = stack.back();
			stack.pop_back();
			if (seg.i1 <= seg.i0 + 1) continue;
			const Vec2 &A = pts[seg.i0];
			const Vec2 &B = pts[seg.i1];
			const float dx = B.x - A.x;
			const float dy = B.y - A.y;
			const float denom = (dx * dx + dy * dy) > 1e-20f ? (dx * dx + dy * dy) : 1e-12f;
			float maxDistSq = -1.0f; size_t idx = seg.i0;
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
		std::vector<Vec2> out; out.reserve(pts.size());
		for (size_t i = 0; i < pts.size(); ++i) if (keep[i]) out.push_back(pts[i]);
		return out;
	}

	static inline int degreeAt(const std::vector<uint8_t> &img, int x, int y, int W, int H)
	{
		if (!img[idxOf(x,y,W)]) return 0;
		int d = 0;
		for (const auto &o : N8)
		{
			int nx = x + o.x, ny = y + o.y;
			if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
			if (img[idxOf(nx,ny,W)]) ++d;
		}
		return d;
	}

}

void SkeletonizeFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
	using clock = std::chrono::high_resolution_clock;
	auto t0 = clock::now();

	out.paths.clear();
	out.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

	const int W0 = static_cast<int>(in.width_px);
	const int H0 = static_cast<int>(in.height_px);
	if (W0 <= 0 || H0 <= 0 || in.pixels.empty())
	{
		out.computeAABB();
		return;
	}

	const uint8_t thresh = static_cast<uint8_t>(std::round(m_parameters.at("threshold").value));
	const int pruneIters = static_cast<int>(std::round(std::max(0.0f, m_parameters.at("pruneIters").value)));
	const float tolPx = std::max(0.0f, m_parameters.at("tolerancePx").value);
	const int down = std::max(1, static_cast<int>(std::round(m_parameters.at("downsample").value)));
	const bool closeLoops = m_parameters.at("closeLoops").value > 0.5f;
	const int turdSizePx = static_cast<int>(std::round(std::max(0.0f, m_parameters.at("turdSizePx").value)));
	const float minSegmentLengthPx = std::max(0.0f, m_parameters.at("minSegmentLengthPx").value);

	// Build binary mask (optionally downsampled with max pooling)
	int W = (W0 + down - 1) / down;
	int H = (H0 + down - 1) / down;
	float pixel_mm = in.pixel_size_mm * static_cast<float>(down);
	std::vector<uint8_t> fg(static_cast<size_t>(W * H), 0);
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			bool any = false;
			const int x0 = x * down;
			const int y0 = y * down;
			for (int yy = y0; yy < std::min(y0 + down, H0) && !any; ++yy)
			{
				for (int xx = x0; xx < std::min(x0 + down, W0); ++xx)
				{
					if (in.pixels[idxOf(xx, yy, W0)] < thresh) { any = true; break; }
				}
			}
			fg[idxOf(x,y,W)] = any ? 1 : 0;
		}
	}

	const float epsMm = tolPx * pixel_mm / static_cast<float>(down); // match effective resolution
	const int turdSizeCells = std::max(0, (down > 0) ? static_cast<int>(std::ceil(static_cast<float>(turdSizePx) / static_cast<float>(down * down))) : turdSizePx);
	const float minSegmentLengthMm = minSegmentLengthPx * in.pixel_size_mm; // defined in input pixels

	// Connected components on downsampled grid (4-connected)
	std::vector<uint8_t> visited(static_cast<size_t>(W * H), 0);
	const IVec2 n4[4] = {{1,0},{-1,0},{0,1},{0,-1}};

	auto hasBackgroundNeighbor = [&](const std::vector<uint8_t> &img, int x, int y, int w, int h) -> bool {
		for (const auto &d : N8)
		{
			int nx = x + d.x, ny = y + d.y;
			if (nx < 0 || ny < 0 || nx >= w || ny >= h) return true;
			if (!img[idxOf(nx,ny,w)]) return true;
		}
		return false;
	};

	auto thin_active = [&](std::vector<uint8_t> &img, int w, int h)
	{
		// Build initial candidate set: edge foreground pixels
		std::vector<int> cand; cand.reserve(static_cast<size_t>(w*h/8 + 1));
		std::vector<uint8_t> inCand(static_cast<size_t>(w * h), 0);
		for (int y = 1; y < h - 1; ++y)
		{
			for (int x = 1; x < w - 1; ++x)
			{
				if (!img[idxOf(x,y,w)]) continue;
				if (hasBackgroundNeighbor(img, x, y, w, h)) { cand.push_back(static_cast<int>(idxOf(x,y,w))); inCand[idxOf(x,y,w)] = 1; }
			}
		}
		if (cand.empty()) return;
		std::vector<int> del; del.reserve(cand.size());
		std::vector<int> nextCand; nextCand.reserve(cand.size());
		std::vector<uint8_t> inNext(static_cast<size_t>(w * h), 0);

		auto phaseDelete = [&](int phase) -> bool {
			del.clear();
			for (int id : cand)
			{
				const int y = id / w; const int x = id % w;
				if (!img[idxOf(x,y,w)]) continue;
				uint8_t p[9]; p[0] = 1; // center is foreground by construction
				for (int k = 0; k < 8; ++k) p[k+1] = img[idxOf(x + N8[k].x, y + N8[k].y, w)];
				int B = 0; for (int k = 1; k <= 8; ++k) B += (p[k] != 0);
				if (B < 2 || B > 6) continue;
				int A = 0; for (int k = 1; k <= 8; ++k) { int k1 = (k % 8) + 1; if (p[k] == 0 && p[k1] == 1) ++A; }
				if (A != 1) continue;
				if (phase == 0)
				{
					if (p[1] * p[3] * p[5] != 0) continue;
					if (p[3] * p[5] * p[7] != 0) continue;
				}
				else
				{
					if (p[1] * p[3] * p[7] != 0) continue;
					if (p[1] * p[5] * p[7] != 0) continue;
				}
				del.push_back(id);
			}
			if (del.empty()) return false;
			// Delete and seed next candidates
			for (int idd : del)
			{
				const int y = idd / w; const int x = idd % w;
				img[idxOf(x,y,w)] = 0;
				for (const auto &d : N8)
				{
					int nx = x + d.x, ny = y + d.y;
					if (nx <= 0 || ny <= 0 || nx >= w-1 || ny >= h-1) continue;
					if (!img[idxOf(nx,ny,w)]) continue;
					const size_t ni = idxOf(nx,ny,w);
					if (!inNext[ni] && hasBackgroundNeighbor(img, nx, ny, w, h))
					{
						nextCand.push_back(static_cast<int>(ni));
						inNext[ni] = 1;
					}
				}
			}
			cand.swap(nextCand);
			nextCand.clear(); std::fill(inNext.begin(), inNext.end(), 0);
			return true;
		};

		int guard = 8 * (w + h); // perimeter-scale safety cap
		while (guard-- > 0)
		{
			bool any = false;
			any |= phaseDelete(0);
			any |= phaseDelete(1);
			if (!any) break;
			if (cand.empty()) break;
		}
	};

	auto trace_component = [&](const std::vector<uint8_t> &roi, int rx, int ry, int rw, int rh)
	{
		std::vector<uint8_t> v(static_cast<size_t>(rw * rh), 0);
		auto nextNeighbor = [&](int x, int y, int px, int py, int &nx, int &ny) -> bool
		{
			int bestQx = 0, bestQy = 0; bool found = false;
			float bestScore = -1e9f;
			int inDx = (px == -9999) ? 0 : (x - px);
			int inDy = (py == -9999) ? 0 : (y - py);
			for (const auto &o : N8)
			{
				int qx = x + o.x, qy = y + o.y;
				if (qx == px && qy == py) continue;
				if (qx < 0 || qy < 0 || qx >= rw || qy >= rh) continue;
				if (!roi[idxOf(qx,qy,rw)]) continue;
				if (px == -9999)
				{
					// no previous direction, accept first
					nx = qx; ny = qy; return true;
				}
				float score = static_cast<float>((qx - x) * inDx + (qy - y) * inDy);
				if (score > bestScore) { bestScore = score; bestQx = qx; bestQy = qy; found = true; }
			}
			if (found) { nx = bestQx; ny = bestQy; return true; }
			return false;
		};

		auto emitPath = [&](const std::vector<IVec2> &pxs, bool closed)
		{
			std::vector<Vec2> pts; pts.reserve(pxs.size());
			// pre-decimate by distance to cut RDP cost
			const float keepDist = std::max(1e-6f, epsMm * 0.5f);
			const float keepDistSq = keepDist * keepDist;
			Vec2 lastKept(-1e9f, -1e9f);
			bool first = true;
			for (const auto &pi : pxs)
			{
				float px_mm = (static_cast<float>(rx + pi.x) + 0.5f) * pixel_mm;
				float py_mm = (static_cast<float>(ry + pi.y) + 0.5f) * pixel_mm;
				if (first)
				{
					pts.emplace_back(px_mm, py_mm);
					lastKept = pts.back();
					first = false;
				}
				else
				{
					float dx = px_mm - lastKept.x;
					float dy = py_mm - lastKept.y;
					if (dx*dx + dy*dy >= keepDistSq)
					{
						pts.emplace_back(px_mm, py_mm);
						lastKept = pts.back();
					}
				}
			}
			if (epsMm > 0.0f && pts.size() > 3) pts = rdp(pts, epsMm);
			if (pts.size() >= 2)
			{
				// compute path length in mm
				float totalLen = 0.0f;
				for (size_t i = 1; i < pts.size(); ++i)
				{
					float dx = pts[i].x - pts[i-1].x;
					float dy = pts[i].y - pts[i-1].y;
					totalLen += std::sqrt(dx*dx + dy*dy);
				}
				if (totalLen < minSegmentLengthMm) return; // drop too-short segments
				Path p; p.closed = (closed && closeLoops); p.points = std::move(pts);
				out.paths.push_back(std::move(p));
			}
		};

		// Trace endpoints first
		for (int y = 0; y < rh; ++y)
		{
			for (int x = 0; x < rw; ++x)
			{
				if (!roi[idxOf(x,y,rw)] || v[idxOf(x,y,rw)]) continue;
				int deg = degreeAt(roi, x, y, rw, rh);
				if (deg != 1) continue;
				std::vector<IVec2> pixels; pixels.reserve(64);
				int cx = x, cy = y; int px = -9999, py = -9999;
				while (true)
				{
					v[idxOf(cx,cy,rw)] = 1;
					pixels.push_back({cx, cy});
					int d = degreeAt(roi, cx, cy, rw, rh);
					if (d != 2) break;
					int nx = cx, ny = cy; if (!nextNeighbor(cx, cy, px, py, nx, ny)) break; px = cx; py = cy; cx = nx; cy = ny;
					if (v[idxOf(cx,cy,rw)]) break;
				}
				emitPath(pixels, false);
			}
		}
		// Trace loops (degree==2)
		for (int y = 0; y < rh; ++y)
		{
			for (int x = 0; x < rw; ++x)
			{
				if (!roi[idxOf(x,y,rw)] || v[idxOf(x,y,rw)]) continue;
				if (degreeAt(roi, x, y, rw, rh) != 2) continue;
				std::vector<IVec2> pixels; pixels.reserve(128);
				int sx = x, sy = y; int cx = x, cy = y; int px = -9999, py = -9999;
				int guard = 8 * (rw + rh);
				while (guard-- > 0)
				{
					v[idxOf(cx,cy,rw)] = 1;
					pixels.push_back({cx, cy});
					int nx = cx, ny = cy; if (!nextNeighbor(cx, cy, px, py, nx, ny)) break;
					// early exit if we're about to revisit a visited pixel (not the canonical close)
					if (v[idxOf(nx,ny,rw)] && !(nx == sx && ny == sy)) break;
					px = cx; py = cy; cx = nx; cy = ny;
					if (cx == sx && cy == sy) { if (closeLoops) pixels.push_back({cx, cy}); break; }
				}
				emitPath(pixels, true);
			}
		}
	};

	// Find components and process each in its ROI with active-set thinning and pruning
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			const size_t ii = idxOf(x,y,W);
			if (visited[ii] || !fg[ii]) continue;
			// BFS collect component
			std::vector<int> q; q.reserve(256);
			std::vector<int> comp; comp.reserve(256);
			q.push_back(static_cast<int>(ii)); visited[ii] = 1; comp.push_back(static_cast<int>(ii));
			int minX = x, minY = y, maxX = x, maxY = y;
			while (!q.empty())
			{
				int p = q.back(); q.pop_back();
				int py = p / W; int px = p % W;
				for (const auto &d : n4)
				{
					int nx = px + d.x, ny = py + d.y; if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
					size_t ni = idxOf(nx,ny,W); if (visited[ni] || !fg[ni]) continue; visited[ni] = 1; q.push_back(static_cast<int>(ni)); comp.push_back(static_cast<int>(ni));
					if (nx < minX) minX = nx; if (nx > maxX) maxX = nx; if (ny < minY) minY = ny; if (ny > maxY) maxY = ny;
				}
			}
			int rw = maxX - minX + 1; int rh = maxY - minY + 1; if (rw <= 0 || rh <= 0) continue;
			if (turdSizeCells > 0 && static_cast<int>(comp.size()) < turdSizeCells) continue;
			std::vector<uint8_t> roi(static_cast<size_t>(rw * rh), 0);
			for (int p : comp)
			{
				int py = p / W; int px = p % W; roi[idxOf(px - minX, py - minY, rw)] = 1;
			}

			thin_active(roi, rw, rh);

			// Endpoint pruning using active set (repeat few iterations)
			for (int it = 0; it < pruneIters; ++it)
			{
				std::vector<int> ends; ends.reserve(128);
				for (int yy = 1; yy < rh - 1; ++yy)
				{
					for (int xx = 1; xx < rw - 1; ++xx)
					{
						if (!roi[idxOf(xx,yy,rw)]) continue;
						if (degreeAt(roi, xx, yy, rw, rh) == 1) ends.push_back(static_cast<int>(idxOf(xx,yy,rw)));
					}
				}
				if (ends.empty()) break;
				for (int idd : ends) roi[idd] = 0;
			}

			trace_component(roi, minX, minY, rw, rh);
		}
	}

	out.computeAABB();
}


