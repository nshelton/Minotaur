#include "FastMarchingFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <queue>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr float kInf = 1e30f;

	// Solve the first-order Eikonal update at a node given the best (min) travel
	// time among its horizontal and vertical KNOWN neighbors and local slowness
	// f = 1/speed (time per pixel). Tx/Ty may be kInf when no neighbor is known.
	float solveEikonal(float Tx, float Ty, float f)
	{
		if (Tx >= kInf && Ty >= kInf) return kInf;
		if (Ty >= kInf) return Tx + f;
		if (Tx >= kInf) return Ty + f;
		float diff = Tx - Ty;
		if (std::fabs(diff) >= f) return std::min(Tx, Ty) + f;
		return 0.5f * (Tx + Ty + std::sqrt(2.0f * f * f - diff * diff));
	}

	int64_t quantKey(const Vec2 &p)
	{
		// Crossing points on a shared cell edge are computed from identical corner
		// values, so they are bit-identical across adjacent cells; quantization is
		// only a safety net. Coordinates are >= 0 (image lives in the first quadrant).
		int64_t ix = static_cast<int64_t>(std::llround(p.x * 1e4));
		int64_t iy = static_cast<int64_t>(std::llround(p.y * 1e4));
		return (ix << 32) | static_cast<int64_t>(static_cast<uint32_t>(iy));
	}

	struct Seg { Vec2 a, b; };

	// Join cell segments end-to-end into polylines / closed loops.
	void stitch(const std::vector<Seg> &segs, PathSet &out)
	{
		const int n = static_cast<int>(segs.size());
		if (n == 0) return;

		std::unordered_map<int64_t, std::vector<int>> byKey;
		byKey.reserve(n * 2);
		for (int i = 0; i < n; ++i)
		{
			byKey[quantKey(segs[i].a)].push_back(i);
			byKey[quantKey(segs[i].b)].push_back(i);
		}

		std::vector<char> used(n, 0);
		auto findNext = [&](const Vec2 &end, int &outSeg) -> bool {
			auto it = byKey.find(quantKey(end));
			if (it == byKey.end()) return false;
			for (int idx : it->second)
				if (!used[idx]) { outSeg = idx; return true; }
			return false;
		};
		auto otherEnd = [&](int i, const Vec2 &end) -> Vec2 {
			return quantKey(segs[i].a) == quantKey(end) ? segs[i].b : segs[i].a;
		};

		for (int s = 0; s < n; ++s)
		{
			if (used[s]) continue;
			used[s] = 1;

			std::deque<Vec2> poly;
			poly.push_back(segs[s].a);
			poly.push_back(segs[s].b);

			for (int ni; findNext(poly.back(), ni); )
			{
				used[ni] = 1;
				poly.push_back(otherEnd(ni, poly.back()));
			}
			for (int ni; findNext(poly.front(), ni); )
			{
				used[ni] = 1;
				poly.push_front(otherEnd(ni, poly.front()));
			}

			Path p;
			p.points.assign(poly.begin(), poly.end());
			if (p.points.size() > 2 &&
			    quantKey(p.points.front()) == quantKey(p.points.back()))
			{
				p.closed = true;
				p.points.pop_back();
			}
			out.paths.push_back(std::move(p));
		}
	}
}

void FastMarchingFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
	out.paths.clear();

	const int w = static_cast<int>(in.width_px);
	const int h = static_cast<int>(in.height_px);
	if (w <= 0 || h <= 0 || in.pixels.empty()) return;

	const float px = in.pixel_size_mm;
	const float minSpeed = std::clamp(m_parameters.at("minSpeed").value, 0.01f, 1.0f);
	const float maxSpeed = std::max(m_parameters.at("maxSpeed").value, minSpeed);
	const float contrast = m_parameters.at("contrast").value;
	const bool invert = m_parameters.at("invert").value > 0.5f;
	const float spacing = std::max(m_parameters.at("levelSpacing").value, 0.005f);
	const int sx = std::clamp(static_cast<int>(m_parameters.at("seedX").value * w), 0, w - 1);
	const int sy = std::clamp(static_cast<int>(m_parameters.at("seedY").value * h), 0, h - 1);

	const int wh = w * h;
	auto idx = [w](int x, int y) { return y * w + x; };

	// Slowness field f = 1/speed per pixel.
	std::vector<float> slow(wh);
	for (int i = 0; i < wh; ++i)
	{
		float b = in.pixels[i] / 255.0f;
		if (invert) b = 1.0f - b;
		b = std::pow(std::clamp(b, 0.0f, 1.0f), contrast);
		float speed = minSpeed + (maxSpeed - minSpeed) * b;
		slow[i] = 1.0f / speed;
	}

	// Fast marching: heap of (T, idx) with lazy deletion.
	std::vector<float> T(wh, kInf);
	std::vector<char> known(wh, 0);
	using HeapItem = std::pair<float, int>;
	std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> heap;

	T[idx(sx, sy)] = 0.0f;
	heap.push({0.0f, idx(sx, sy)});

	const int nbx[4] = {-1, 1, 0, 0};
	const int nby[4] = {0, 0, -1, 1};

	while (!heap.empty())
	{
		HeapItem top = heap.top();
		heap.pop();
		int i = top.second;
		if (top.first != T[i]) continue; // stale
		if (known[i]) continue;
		known[i] = 1;

		int cx = i % w, cy = i / w;
		for (int k = 0; k < 4; ++k)
		{
			int nx = cx + nbx[k], ny = cy + nby[k];
			if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
			int j = idx(nx, ny);
			if (known[j]) continue;

			float Tx = kInf, Ty = kInf;
			if (nx > 0 && known[j - 1]) Tx = std::min(Tx, T[j - 1]);
			if (nx < w - 1 && known[j + 1]) Tx = std::min(Tx, T[j + 1]);
			if (ny > 0 && known[j - w]) Ty = std::min(Ty, T[j - w]);
			if (ny < h - 1 && known[j + w]) Ty = std::min(Ty, T[j + w]);

			float cand = solveEikonal(Tx, Ty, slow[j]);
			if (cand < T[j])
			{
				T[j] = cand;
				heap.push({cand, j});
			}
		}
	}

	// Normalize finite travel times to [0, 1].
	float tmax = 0.0f;
	for (int i = 0; i < wh; ++i)
		if (T[i] < kInf) tmax = std::max(tmax, T[i]);
	if (tmax <= 0.0f) return;
	std::vector<float> Tn(wh);
	for (int i = 0; i < wh; ++i)
		Tn[i] = (T[i] < kInf) ? (T[i] / tmax) : kInf;

	// Marching squares per level, then stitch into polylines.
	const int maxLevels = 512;
	for (int level = 1; level <= maxLevels; ++level)
	{
		float L = level * spacing;
		if (L >= 1.0f) break;

		std::vector<Seg> segs;
		for (int y = 0; y < h - 1; ++y)
		{
			for (int x = 0; x < w - 1; ++x)
			{
				// a=TL, b=TR, c=BR, d=BL
				float a = Tn[idx(x, y)];
				float b = Tn[idx(x + 1, y)];
				float c = Tn[idx(x + 1, y + 1)];
				float d = Tn[idx(x, y + 1)];
				if (a >= kInf || b >= kInf || c >= kInf || d >= kInf) continue;

				bool ea = a < L, eb = b < L, ec = c < L, ed = d < L;
				int below = ea + eb + ec + ed;
				if (below == 0 || below == 4) continue;

				bool eTop = ea != eb, eRight = eb != ec, eBottom = ec != ed, eLeft = ed != ea;
				Vec2 pTop, pRight, pBottom, pLeft;
				if (eTop) pTop = Vec2((x + (L - a) / (b - a)) * px, y * px);
				if (eRight) pRight = Vec2((x + 1) * px, (y + (L - b) / (c - b)) * px);
				if (eBottom) pBottom = Vec2((x + (L - d) / (c - d)) * px, (y + 1) * px);
				if (eLeft) pLeft = Vec2(x * px, (y + (L - a) / (d - a)) * px);

				int nEdges = eTop + eRight + eBottom + eLeft;
				if (nEdges == 2)
				{
					Vec2 pts[2];
					int kk = 0;
					if (eTop) pts[kk++] = pTop;
					if (eRight) pts[kk++] = pRight;
					if (eBottom) pts[kk++] = pBottom;
					if (eLeft) pts[kk++] = pLeft;
					segs.push_back({pts[0], pts[1]});
				}
				else // saddle: all four edges cross
				{
					bool centerBelow = (a + b + c + d) * 0.25f < L;
					if (ea && ec) // TL,BR below
					{
						if (centerBelow) { segs.push_back({pTop, pRight}); segs.push_back({pBottom, pLeft}); }
						else { segs.push_back({pLeft, pTop}); segs.push_back({pRight, pBottom}); }
					}
					else // TR,BL below
					{
						if (centerBelow) { segs.push_back({pTop, pLeft}); segs.push_back({pRight, pBottom}); }
						else { segs.push_back({pTop, pRight}); segs.push_back({pBottom, pLeft}); }
					}
				}
			}
		}
		stitch(segs, out);
	}

	out.computeAABB();
}
