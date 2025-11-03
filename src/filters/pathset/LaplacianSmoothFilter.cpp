#include "filters/pathset/LaplacianSmoothFilter.h"

#include <algorithm>
#include <vector>

static inline Vec2 lerpVec2(const Vec2 &a, const Vec2 &b, float t)
{
	return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

static void laplacianOnce(const Path &in, Path &out, float weight)
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

	const float w = std::clamp(weight, 0.0f, 1.0f);

	if (!in.closed)
	{
		// Preserve endpoints for open paths
		out.points.push_back(in.points[0]);
		if (n > 2)
		{
			for (size_t i = 1; i + 1 < n; ++i)
			{
				const Vec2 &pPrev = in.points[i - 1];
				const Vec2 &pNext = in.points[i + 1];
				Vec2 avg = Vec2((pPrev.x + pNext.x) * 0.5f, (pPrev.y + pNext.y) * 0.5f);
				Vec2 cur = in.points[i];
				Vec2 smoothed = lerpVec2(cur, avg, w);
				out.points.push_back(smoothed);
			}
		}
		if (n >= 2)
			out.points.push_back(in.points[n - 1]);
	}
	else
	{
		// Closed path: wrap-around neighbors
		out.points.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			size_t ip = (i + n - 1) % n;
			size_t inx = (i + 1) % n;
			const Vec2 &pPrev = in.points[ip];
			const Vec2 &pNext = in.points[inx];
			Vec2 avg = Vec2((pPrev.x + pNext.x) * 0.5f, (pPrev.y + pNext.y) * 0.5f);
			Vec2 cur = in.points[i];
			Vec2 smoothed = lerpVec2(cur, avg, w);
			out.points.push_back(smoothed);
		}
	}
}

void LaplacianSmoothFilter::applyTyped(const PathSet &in, PathSet &out) const
{
	out.color = in.color;
	out.paths.clear();
	out.paths.reserve(in.paths.size());

	int iters = static_cast<int>(m_parameters.at("iterations").value + 0.5f);
	if (iters < 0) iters = 0;
	if (iters > 50) iters = 50;
	float weight = std::clamp(m_parameters.at("weight").value, 0.0f, 1.0f);

	for (const Path &p : in.paths)
	{
		if (p.points.size() < 2 || iters == 0 || weight <= 0.0f)
		{
			out.paths.push_back(p);
			continue;
		}

		Path cur = p;
		Path tmp;
		for (int i = 0; i < iters; ++i)
		{
			laplacianOnce(cur, tmp, weight);
			cur.points.swap(tmp.points);
			cur.closed = tmp.closed;
		}
		out.paths.push_back(std::move(cur));
	}

	out.computeAABB();
}



