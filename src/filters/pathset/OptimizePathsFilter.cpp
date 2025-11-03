#include "filters/pathset/OptimizePathsFilter.h"

#include <algorithm>
#include <cmath>
#include <limits>

static inline float distanceSquared(const Vec2 &a, const Vec2 &b)
{
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

static inline void reversePath(Path &p)
{
	std::reverse(p.points.begin(), p.points.end());
}

void OptimizePathsFilter::applyTyped(const PathSet &in, PathSet &out) const
{
	out.color = in.color;
	out.paths.clear();

	const bool mergeEnabled = m_parameters.at("mergeEnabled").value > 0.5f;
	const float mergeDist = std::max(0.0f, m_parameters.at("mergeDistanceMm").value);
	const bool doReorder = m_parameters.at("reorder").value > 0.5f;
	const float mergeDist2 = mergeDist * mergeDist;
	const size_t maxMergePaths = 4000; // safety guard to avoid O(n^2) on huge sets

	// Copy input paths
	std::vector<Path> working = in.paths;

	// Merge pass (guarded)
	if (mergeEnabled && mergeDist > 0.0f && working.size() > 1 && working.size() <= maxMergePaths)
	{
		bool changed = true;
		while (changed)
		{
			changed = false;
			const size_t n = working.size();
			if (n < 2) break;

			for (size_t i = 0; i < n && !changed; ++i)
			{
				Path &pi = working[i];
				if (pi.closed || pi.points.size() == 0) continue;

				const Vec2 istart = pi.points.front();
				const Vec2 iend = pi.points.back();

				size_t bestJ = static_cast<size_t>(-1);
				enum class AttachKind { Append, AppendReverseJ, Prepend, PrependReverseJ } bestKind = AttachKind::Append;
				float bestD2 = std::numeric_limits<float>::infinity();

				for (size_t j = 0; j < n; ++j)
				{
					if (j == i) continue;
					const Path &pj = working[j];
					if (pj.closed || pj.points.size() == 0) continue;

					const Vec2 jstart = pj.points.front();
					const Vec2 jend = pj.points.back();

					float d2;
					d2 = distanceSquared(iend, jstart);
					if (d2 < bestD2) { bestD2 = d2; bestKind = AttachKind::Append; bestJ = j; }

					d2 = distanceSquared(iend, jend);
					if (d2 < bestD2) { bestD2 = d2; bestKind = AttachKind::AppendReverseJ; bestJ = j; }

					d2 = distanceSquared(istart, jend);
					if (d2 < bestD2) { bestD2 = d2; bestKind = AttachKind::Prepend; bestJ = j; }

					d2 = distanceSquared(istart, jstart);
					if (d2 < bestD2) { bestD2 = d2; bestKind = AttachKind::PrependReverseJ; bestJ = j; }
				}

				if (bestJ != static_cast<size_t>(-1) && bestD2 <= mergeDist2)
				{
					Path &pj = working[bestJ];
					if (bestKind == AttachKind::Append)
					{
						if (!pj.points.empty())
						{
							pi.points.insert(pi.points.end(), pj.points.begin(), pj.points.end());
						}
					}
					else if (bestKind == AttachKind::AppendReverseJ)
					{
						if (!pj.points.empty())
						{
							Path tmp = pj;
							reversePath(tmp);
							pi.points.insert(pi.points.end(), tmp.points.begin(), tmp.points.end());
						}
					}
					else if (bestKind == AttachKind::Prepend)
					{
						if (!pj.points.empty())
						{
							std::vector<Vec2> merged;
							merged.reserve(pj.points.size() + pi.points.size());
							merged.insert(merged.end(), pj.points.begin(), pj.points.end());
							merged.insert(merged.end(), pi.points.begin(), pi.points.end());
							pi.points.swap(merged);
						}
					}
					else // PrependReverseJ
					{
						if (!pj.points.empty())
						{
							Path tmp = pj;
							reversePath(tmp);
							std::vector<Vec2> merged;
							merged.reserve(tmp.points.size() + pi.points.size());
							merged.insert(merged.end(), tmp.points.begin(), tmp.points.end());
							merged.insert(merged.end(), pi.points.begin(), pi.points.end());
							pi.points.swap(merged);
						}
					}

					if (bestJ > i)
					{
						working.erase(working.begin() + static_cast<std::ptrdiff_t>(bestJ));
					}
					else
					{
						working.erase(working.begin() + static_cast<std::ptrdiff_t>(bestJ));
					}
					changed = true;
				}
			}
		}
	}

	// Reorder pass: nearest-neighbor traversal allowing flips for open paths (brute-force)
	if (!doReorder)
	{
		out.paths = std::move(working);
		out.computeAABB();
		return;
	}

	in.computeAABB();
	Vec2 ref = in.aabb.min;

	const size_t m = working.size();
	std::vector<bool> used(m, false);
	out.paths.reserve(m);

	// Pick the first path by closest endpoint to ref
	size_t startIdx = 0;
	float bestFirst = std::numeric_limits<float>::infinity();
	bool reverseFirst = false;
	for (size_t i = 0; i < m; ++i)
	{
		const Path &p = working[i];
		if (p.points.empty()) continue;
		float dStart = distanceSquared(ref, p.points.front());
		float dEnd = distanceSquared(ref, p.points.back());
		float d = std::min(dStart, dEnd);
		if (d < bestFirst)
		{
			bestFirst = d;
			startIdx = i;
			reverseFirst = (dEnd < dStart) && (!p.closed);
		}
	}

	Vec2 lastPoint = ref;
	{
		Path first = working[startIdx];
		used[startIdx] = true;
		if (reverseFirst)
			reversePath(first);
		lastPoint = first.points.empty() ? lastPoint : first.points.back();
		out.paths.push_back(std::move(first));
	}

	for (size_t k = 1; k < m; ++k)
	{
		size_t bestIdx = static_cast<size_t>(-1);
		bool reverseCandidate = false;
		float bestD2 = std::numeric_limits<float>::infinity();

		for (size_t i = 0; i < m; ++i)
		{
			if (used[i]) continue;
			const Path &p = working[i];
			if (p.points.empty()) continue;

			float dStart = distanceSquared(lastPoint, p.points.front());
			float dEnd = distanceSquared(lastPoint, p.points.back());
			float cand = dStart;
			bool candReverse = false;
			if (!p.closed && dEnd < dStart) { cand = dEnd; candReverse = true; }

			if (cand < bestD2)
			{
				bestD2 = cand;
				bestIdx = i;
				reverseCandidate = candReverse;
			}
		}

		if (bestIdx == static_cast<size_t>(-1))
			break;

		Path chosen = working[bestIdx];
		used[bestIdx] = true;
		if (reverseCandidate)
			reversePath(chosen);
		lastPoint = chosen.points.empty() ? lastPoint : chosen.points.back();
		out.paths.push_back(std::move(chosen));
	}

	out.computeAABB();
}


