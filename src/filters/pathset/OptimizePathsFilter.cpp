#include "filters/pathset/OptimizePathsFilter.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "utils/nanoflann.hpp"

// Build endpoint cloud (two endpoints per path)
struct EndpointCloud
{
	std::vector<Vec2> pts;
	inline size_t kdtree_get_point_count() const { return pts.size(); }
	inline float kdtree_get_pt(const size_t idx, const size_t dim) const
	{
		return dim == 0 ? pts[idx].x : pts[idx].y;
	}
	template <class BBOX>
	bool kdtree_get_bbox(BBOX&) const { return false; }
};

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

	// Merge pass accelerated by KD-trees over endpoints (greedy until convergence)
	if (mergeEnabled && mergeDist > 0.0f && working.size() > 1)
	{
		const size_t n = working.size();
		std::vector<bool> active(n, true);

		// Build KD-trees over initial endpoints (we'll skip inactive paths on queries)
		EndpointCloud cloudStarts;
		EndpointCloud cloudEnds;
		cloudStarts.pts.reserve(n);
		cloudEnds.pts.reserve(n);
		std::vector<size_t> startToPath; startToPath.reserve(n);
		std::vector<size_t> endToPath; endToPath.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			const Path &p = working[i];
			if (p.points.empty()) { active[i] = false; continue; }
			cloudStarts.pts.push_back(p.points.front());
			startToPath.push_back(i);
			cloudEnds.pts.push_back(p.points.back());
			endToPath.push_back(i);
		}

		using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
			nanoflann::L2_Simple_Adaptor<float, EndpointCloud>, EndpointCloud, 2>;
		KDTree indexStarts(2, cloudStarts, {10});
		KDTree indexEnds(2, cloudEnds, {10});
		// static adaptor builds at construction

		enum class AttachKind { Append, AppendReverseJ, Prepend, PrependReverseJ };

		auto queryNearest = [&](const Vec2 &q, bool queryStarts, size_t kInit, size_t selfIdx,
			const std::vector<size_t> &map, KDTree &tree, float &outD2, size_t &outJ) -> bool
		{
			const size_t total = queryStarts ? cloudStarts.pts.size() : cloudEnds.pts.size();
			if (total == 0) return false;
			float query_pt[2] = { q.x, q.y };
			size_t k = std::min(kInit, total);
			while (true)
			{
				std::vector<size_t> idx(k);
				std::vector<float> dist(k);
				nanoflann::KNNResultSet<float> rs(k);
				rs.init(idx.data(), dist.data());
				tree.findNeighbors(rs, query_pt, {10});
				const size_t got = rs.size();
				for (size_t ii = 0; ii < got; ++ii)
				{
					const size_t ep = idx[ii];
					const size_t pj = map[ep];
					if (pj == selfIdx) continue;
					if (!active[pj]) continue;
					const Path &cand = working[pj];
					if (cand.closed || cand.points.empty()) continue;
					outD2 = dist[ii];
					outJ = pj;
					return true;
				}
				if (k == total) break;
				k = std::min<size_t>(k * 2, total);
			}
			return false;
		};

		for (size_t i = 0; i < n; ++i)
		{
			if (!active[i]) continue;
			Path &pi = working[i];
			if (pi.closed || pi.points.size() == 0) { continue; }

			bool mergedAny = false;
			while (true)
			{
				if (pi.points.empty()) break;
				const Vec2 istart = pi.points.front();
				const Vec2 iend = pi.points.back();

				float bestD2 = std::numeric_limits<float>::infinity();
				size_t bestJ = static_cast<size_t>(-1);
				AttachKind bestKind = AttachKind::Append;

				// iend -> jstart (Append)
				{
					float d2; size_t j;
					if (queryNearest(iend, true, 16, i, startToPath, indexStarts, d2, j) && d2 < bestD2)
					{ bestD2 = d2; bestJ = j; bestKind = AttachKind::Append; }
				}
				// iend -> jend (AppendReverseJ)
				{
					float d2; size_t j;
					if (queryNearest(iend, false, 16, i, endToPath, indexEnds, d2, j) && d2 < bestD2)
					{ bestD2 = d2; bestJ = j; bestKind = AttachKind::AppendReverseJ; }
				}
				// istart -> jend (Prepend)
				{
					float d2; size_t j;
					if (queryNearest(istart, false, 16, i, endToPath, indexEnds, d2, j) && d2 < bestD2)
					{ bestD2 = d2; bestJ = j; bestKind = AttachKind::Prepend; }
				}
				// istart -> jstart (PrependReverseJ)
				{
					float d2; size_t j;
					if (queryNearest(istart, true, 16, i, startToPath, indexStarts, d2, j) && d2 < bestD2)
					{ bestD2 = d2; bestJ = j; bestKind = AttachKind::PrependReverseJ; }
				}

				if (bestJ == static_cast<size_t>(-1) || bestD2 > mergeDist2)
					break;

				Path &pj = working[bestJ];
				if (bestKind == AttachKind::Append)
				{
					if (!pj.points.empty())
						pi.points.insert(pi.points.end(), pj.points.begin(), pj.points.end());
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

				active[bestJ] = false;
				mergedAny = true;
			}
			(void)mergedAny;
		}

		// Compact the working set to only active paths
		std::vector<Path> compacted;
		compacted.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			if (active[i])
				compacted.push_back(std::move(working[i]));
		}
		working.swap(compacted);
	}

	// Reorder pass: nearest-neighbor traversal using a dynamic KD-tree over path endpoints
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



	// Two endpoint clouds: starts and ends
	EndpointCloud cloudStarts;
	EndpointCloud cloudEnds;
	cloudStarts.pts.reserve(m);
	cloudEnds.pts.reserve(m);
	std::vector<size_t> startToPath; startToPath.reserve(m);
	std::vector<size_t> endToPath; endToPath.reserve(m);
	for (size_t i = 0; i < m; ++i)
	{
		const Path &p = working[i];
		if (p.points.empty()) continue;
		cloudStarts.pts.push_back(p.points.front());
		startToPath.push_back(i);
		cloudEnds.pts.push_back(p.points.back());
		endToPath.push_back(i);
	}

	using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
		nanoflann::L2_Simple_Adaptor<float, EndpointCloud>, EndpointCloud, 2>;
	KDTree indexStarts(2, cloudStarts, {10});
	KDTree indexEnds(2, cloudEnds, {10});
	// static adaptor builds at construction
	// static adaptor builds at construction; no addPoints needed

	auto pickNearest = [&](const Vec2 &q) -> std::pair<size_t, bool>
	{
		if (cloudStarts.pts.empty() && cloudEnds.pts.empty()) return {static_cast<size_t>(-1), false};
		float query_pt[2] = { q.x, q.y };

		// progressively widen the search for both trees
		size_t kS = std::min<size_t>(16, cloudStarts.pts.size());
		size_t kE = std::min<size_t>(16, cloudEnds.pts.size());
		while (true)
		{
			// current best
			size_t bestIdx = static_cast<size_t>(-1);
			bool bestReverse = false;
			float bestD2 = std::numeric_limits<float>::infinity();

			if (kS > 0)
			{
				std::vector<size_t> idxS(kS);
				std::vector<float> distS(kS);
				nanoflann::KNNResultSet<float> rsS(kS);
				rsS.init(idxS.data(), distS.data());
				indexStarts.findNeighbors(rsS, query_pt, {10});
				const size_t gotS = rsS.size();
				for (size_t ii = 0; ii < gotS; ++ii)
				{
					const size_t ep = idxS[ii];
					const size_t pi = startToPath[ep];
					if (used[pi]) continue;
					const float d2 = distS[ii];
					if (d2 < bestD2)
					{
						bestD2 = d2;
						bestIdx = pi;
						bestReverse = false;
					}
				}
			}

			if (kE > 0)
			{
				std::vector<size_t> idxE(kE);
				std::vector<float> distE(kE);
				nanoflann::KNNResultSet<float> rsE(kE);
				rsE.init(idxE.data(), distE.data());
				indexEnds.findNeighbors(rsE, query_pt, {10});
				const size_t gotE = rsE.size();
				for (size_t ii = 0; ii < gotE; ++ii)
				{
					const size_t ep = idxE[ii];
					const size_t pi = endToPath[ep];
					if (used[pi]) continue;
					const float d2 = distE[ii];
					const Path &p = working[pi];
					const bool canReverse = !p.closed;
					if (d2 < bestD2)
					{
						bestD2 = d2;
						bestIdx = pi;
						bestReverse = canReverse;
					}
				}
			}

			if (bestIdx != static_cast<size_t>(-1))
				return {bestIdx, bestReverse};

			const bool canGrowS = kS < cloudStarts.pts.size();
			const bool canGrowE = kE < cloudEnds.pts.size();
			if (!canGrowS && !canGrowE) break;
			if (canGrowS) kS = std::min<size_t>(kS ? kS * 2 : 16, cloudStarts.pts.size());
			if (canGrowE) kE = std::min<size_t>(kE ? kE * 2 : 16, cloudEnds.pts.size());
		}

		return {static_cast<size_t>(-1), false};
	};

	// Pick first path via KD-tree
	Vec2 lastPoint = ref;
	{
		auto [startIdx, reverseFirst] = pickNearest(ref);
		if (startIdx == static_cast<size_t>(-1))
		{
			out.paths = std::move(working);
			out.computeAABB();
			return;
		}
		Path first = working[startIdx];
		used[startIdx] = true;
		if (reverseFirst)
			reversePath(first);
		lastPoint = first.points.empty() ? lastPoint : first.points.back();
		out.paths.push_back(std::move(first));
	}

	for (size_t picked = 1; picked < m; ++picked)
	{
		auto [bestIdx, reverseCandidate] = pickNearest(lastPoint);
		if (bestIdx == static_cast<size_t>(-1)) break;
		Path chosen = working[bestIdx];
		used[bestIdx] = true;
		if (reverseCandidate)
			reversePath(chosen);
		lastPoint = chosen.points.empty() ? lastPoint : chosen.points.back();
		out.paths.push_back(std::move(chosen));
	}

	out.computeAABB();
}


