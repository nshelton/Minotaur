#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "utils/ObjLoader.h"

// Quadric Error Metric (QEM) mesh decimation via edge collapse.
// Implements the Garland & Heckbert algorithm:
//   1. Compute per-vertex quadric matrices from incident face planes
//   2. For each edge, compute optimal collapse position + error cost
//   3. Greedily collapse lowest-cost edges via priority queue
//   4. Return a new ObjMesh with reduced face count
//
// Usage:
//   ObjMesh decimated = MeshDecimator::decimate(mesh, 0.5f);
//   // ratio 0 = keep all, 1 = maximum decimation

namespace MeshDecimator {

// ── Vec3 helpers (ObjMesh::Vec3 has no operators) ───────────────────

using V3 = ObjMesh::Vec3;

inline V3 v3sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 v3add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 v3scale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 v3cross(V3 a, V3 b)
{
	return {a.y * b.z - a.z * b.y,
	        a.z * b.x - a.x * b.z,
	        a.x * b.y - a.y * b.x};
}
inline float v3length(V3 a) { return std::sqrt(v3dot(a, a)); }

// ── Symmetric 4x4 matrix (quadric) ─────────────────────────────────
// Stored as 10 unique values of the upper triangle:
//   [ q0  q1  q2  q3 ]
//   [ q1  q4  q5  q6 ]
//   [ q2  q5  q7  q8 ]
//   [ q3  q6  q8  q9 ]

struct SymMat4
{
	double q[10] = {};

	static SymMat4 zero() { return SymMat4{}; }

	// Build quadric from plane equation (a, b, c, d) where ax+by+cz+d=0
	static SymMat4 fromPlane(double a, double b, double c, double d)
	{
		SymMat4 m;
		m.q[0] = a * a; m.q[1] = a * b; m.q[2] = a * c; m.q[3] = a * d;
		m.q[4] = b * b; m.q[5] = b * c; m.q[6] = b * d;
		m.q[7] = c * c; m.q[8] = c * d;
		m.q[9] = d * d;
		return m;
	}

	SymMat4 operator+(const SymMat4 &o) const
	{
		SymMat4 r;
		for (int i = 0; i < 10; ++i) r.q[i] = q[i] + o.q[i];
		return r;
	}

	SymMat4 &operator+=(const SymMat4 &o)
	{
		for (int i = 0; i < 10; ++i) q[i] += o.q[i];
		return *this;
	}

	// Evaluate v^T * Q * v  (v is homogeneous [x, y, z, 1])
	double evaluate(double x, double y, double z) const
	{
		return q[0]*x*x + 2*q[1]*x*y + 2*q[2]*x*z + 2*q[3]*x
		     + q[4]*y*y + 2*q[5]*y*z + 2*q[6]*y
		     + q[7]*z*z + 2*q[8]*z
		     + q[9];
	}

	double evaluate(V3 v) const { return evaluate(v.x, v.y, v.z); }

	// Solve for the optimal position minimizing v^T Q v.
	// Returns false if the 3x3 sub-matrix is singular.
	bool solveOptimal(double &ox, double &oy, double &oz) const
	{
		// The 3x3 system from dQ/dv = 0:
		// [ q0 q1 q2 ] [x]   [-q3]
		// [ q1 q4 q5 ] [y] = [-q6]
		// [ q2 q5 q7 ] [z]   [-q8]
		double a00 = q[0], a01 = q[1], a02 = q[2];
		double a11 = q[4], a12 = q[5];
		double a22 = q[7];

		double det = a00 * (a11 * a22 - a12 * a12)
		           - a01 * (a01 * a22 - a12 * a02)
		           + a02 * (a01 * a12 - a11 * a02);

		if (std::fabs(det) < 1e-15) return false;

		double inv = 1.0 / det;
		// Cofactors for inverse
		double c00 = (a11 * a22 - a12 * a12) * inv;
		double c01 = (a02 * a12 - a01 * a22) * inv;
		double c02 = (a01 * a12 - a02 * a11) * inv;
		double c11 = (a00 * a22 - a02 * a02) * inv;
		double c12 = (a02 * a01 - a00 * a12) * inv;
		double c22 = (a00 * a11 - a01 * a01) * inv;

		double bx = -q[3], by = -q[6], bz = -q[8];
		ox = c00 * bx + c01 * by + c02 * bz;
		oy = c01 * bx + c11 * by + c12 * bz;
		oz = c02 * bx + c12 * by + c22 * bz;
		return true;
	}
};

// ── Edge record for priority queue ──────────────────────────────────

struct EdgeRecord
{
	int v0, v1;
	V3 optimalPos;
	double cost;
	int generation; // for lazy deletion
};

// ── Main decimation function ────────────────────────────────────────

inline ObjMesh decimate(const ObjMesh &mesh, float ratio)
{
	if (ratio <= 0.0f || mesh.empty()) return mesh;

	// Step 1: Fan-triangulate all faces
	struct Tri { int v[3]; };
	std::vector<Tri> triangles;
	triangles.reserve(mesh.faces.size());
	for (const auto &face : mesh.faces)
	{
		int nv = static_cast<int>(face.indices.size());
		if (nv < 3) continue;
		for (int i = 1; i < nv - 1; ++i)
			triangles.push_back({{face.indices[0], face.indices[i], face.indices[i + 1]}});
	}

	int numVerts = static_cast<int>(mesh.vertices.size());
	int numTris = static_cast<int>(triangles.size());
	int targetTris = std::max(4, static_cast<int>(numTris * (1.0f - ratio)));

	if (numTris <= targetTris) return mesh;

	// Mutable vertex positions
	std::vector<V3> verts(mesh.vertices.begin(), mesh.vertices.end());

	// Step 2: Compute per-vertex quadrics from face planes
	std::vector<SymMat4> quadrics(numVerts, SymMat4::zero());

	for (const auto &tri : triangles)
	{
		V3 e1 = v3sub(verts[tri.v[1]], verts[tri.v[0]]);
		V3 e2 = v3sub(verts[tri.v[2]], verts[tri.v[0]]);
		V3 n = v3cross(e1, e2);
		float len = v3length(n);
		if (len < 1e-12f) continue;
		n = v3scale(n, 1.0f / len);
		double a = n.x, b = n.y, c = n.z;
		double d = -(a * verts[tri.v[0]].x + b * verts[tri.v[0]].y + c * verts[tri.v[0]].z);
		SymMat4 Kp = SymMat4::fromPlane(a, b, c, d);
		quadrics[tri.v[0]] += Kp;
		quadrics[tri.v[1]] += Kp;
		quadrics[tri.v[2]] += Kp;
	}

	// Step 3: Build adjacency structures
	// vertex -> face indices
	std::vector<std::vector<int>> vertFaces(numVerts);
	for (int fi = 0; fi < numTris; ++fi)
	{
		vertFaces[triangles[fi].v[0]].push_back(fi);
		vertFaces[triangles[fi].v[1]].push_back(fi);
		vertFaces[triangles[fi].v[2]].push_back(fi);
	}

	std::vector<bool> triDeleted(numTris, false);
	std::vector<bool> vertDeleted(numVerts, false);

	// Boundary detection: edges with only one adjacent face get penalty planes
	{
		std::unordered_map<int64_t, int> edgeFaceCount;
		auto edgeKey = [](int a, int b) -> int64_t
		{
			if (a > b) std::swap(a, b);
			return (static_cast<int64_t>(a) << 32) | static_cast<int64_t>(b);
		};
		for (int fi = 0; fi < numTris; ++fi)
		{
			const auto &t = triangles[fi];
			int idx[3] = {t.v[0], t.v[1], t.v[2]};
			for (int i = 0; i < 3; ++i)
			{
				int64_t key = edgeKey(idx[i], idx[(i + 1) % 3]);
				edgeFaceCount[key]++;
			}
		}

		constexpr double BOUNDARY_WEIGHT = 10.0;
		for (auto &[key, count] : edgeFaceCount)
		{
			if (count != 1) continue;
			int a = static_cast<int>(key >> 32);
			int b = static_cast<int>(key & 0xFFFFFFFF);

			// Find the adjacent triangle to get the face normal
			V3 edgeDir = v3sub(verts[b], verts[a]);
			float edgeLen = v3length(edgeDir);
			if (edgeLen < 1e-12f) continue;

			// Build a penalty plane perpendicular to the face, containing the edge
			// We need the face normal from any triangle using this edge
			V3 faceNormal = {0, 0, 0};
			for (int fi : vertFaces[a])
			{
				const auto &t = triangles[fi];
				int idx[3] = {t.v[0], t.v[1], t.v[2]};
				bool hasB = (idx[0] == b || idx[1] == b || idx[2] == b);
				if (!hasB) continue;
				V3 e1 = v3sub(verts[idx[1]], verts[idx[0]]);
				V3 e2 = v3sub(verts[idx[2]], verts[idx[0]]);
				faceNormal = v3cross(e1, e2);
				float fnLen = v3length(faceNormal);
				if (fnLen > 1e-12f)
				{
					faceNormal = v3scale(faceNormal, 1.0f / fnLen);
					break;
				}
			}

			// Penalty plane normal = face_normal x edge_dir (perpendicular to both)
			V3 penaltyN = v3cross(faceNormal, edgeDir);
			float pnLen = v3length(penaltyN);
			if (pnLen < 1e-12f) continue;
			penaltyN = v3scale(penaltyN, 1.0f / pnLen);

			double pa = penaltyN.x, pb = penaltyN.y, pc = penaltyN.z;
			double pd = -(pa * verts[a].x + pb * verts[a].y + pc * verts[a].z);
			SymMat4 penalty = SymMat4::fromPlane(pa, pb, pc, pd);
			for (int i = 0; i < 10; ++i) penalty.q[i] *= BOUNDARY_WEIGHT;
			quadrics[a] += penalty;
			quadrics[b] += penalty;
		}
	}

	// Step 4: Build edge list and compute initial costs
	struct EdgeInfo
	{
		int v0, v1;
		V3 optimalPos;
		double cost;
		int gen;
	};

	auto computeEdgeCost = [&](int va, int vb) -> std::pair<V3, double>
	{
		SymMat4 Qbar = quadrics[va] + quadrics[vb];
		double ox, oy, oz;
		V3 optimal;
		double cost;

		if (Qbar.solveOptimal(ox, oy, oz))
		{
			optimal = {static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(oz)};
			cost = Qbar.evaluate(ox, oy, oz);
		}
		else
		{
			// Fallback: pick best of endpoints and midpoint
			V3 mid = v3scale(v3add(verts[va], verts[vb]), 0.5f);
			double ca = Qbar.evaluate(verts[va]);
			double cb = Qbar.evaluate(verts[vb]);
			double cm = Qbar.evaluate(mid);
			if (ca <= cb && ca <= cm) { optimal = verts[va]; cost = ca; }
			else if (cb <= cm)        { optimal = verts[vb]; cost = cb; }
			else                      { optimal = mid;       cost = cm; }
		}

		if (cost < 0.0) cost = 0.0;
		return {optimal, cost};
	};

	// Collect unique edges from triangles
	std::vector<EdgeInfo> edges;
	{
		std::unordered_set<int64_t> seen;
		auto edgeKey = [](int a, int b) -> int64_t
		{
			if (a > b) std::swap(a, b);
			return (static_cast<int64_t>(a) << 32) | static_cast<int64_t>(b);
		};
		for (const auto &tri : triangles)
		{
			int idx[3] = {tri.v[0], tri.v[1], tri.v[2]};
			for (int i = 0; i < 3; ++i)
			{
				int a = idx[i], b = idx[(i + 1) % 3];
				int64_t key = edgeKey(a, b);
				if (seen.insert(key).second)
				{
					if (a > b) std::swap(a, b);
					auto [opt, cost] = computeEdgeCost(a, b);
					edges.push_back({a, b, opt, cost, 0});
				}
			}
		}
	}

	// vertex -> edge indices (for updating neighbors after collapse)
	std::vector<std::vector<int>> vertEdges(numVerts);
	for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei)
	{
		vertEdges[edges[ei].v0].push_back(ei);
		vertEdges[edges[ei].v1].push_back(ei);
	}

	// Priority queue: (cost, edge index, generation)
	struct PQEntry
	{
		double cost;
		int edgeIdx;
		int gen;
		bool operator>(const PQEntry &o) const { return cost > o.cost; }
	};
	std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
	for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei)
		pq.push({edges[ei].cost, ei, 0});

	// Step 5: Collapse loop
	int liveTris = numTris;

	while (liveTris > targetTris && !pq.empty())
	{
		auto top = pq.top();
		pq.pop();

		int ei = top.edgeIdx;
		if (top.gen != edges[ei].gen) continue; // stale entry
		if (vertDeleted[edges[ei].v0] || vertDeleted[edges[ei].v1]) continue;

		int va = edges[ei].v0;
		int vb = edges[ei].v1;
		if (va == vb) continue; // self-loop from prior remapping

		// Collapse vb into va: move va to optimal position
		verts[va] = edges[ei].optimalPos;
		quadrics[va] = quadrics[va] + quadrics[vb];
		vertDeleted[vb] = true;

		// Update faces: replace vb with va, detect degenerate triangles
		for (int fi : vertFaces[vb])
		{
			if (triDeleted[fi]) continue;
			auto &tri = triangles[fi];

			// Replace vb -> va
			for (int k = 0; k < 3; ++k)
				if (tri.v[k] == vb) tri.v[k] = va;

			// Check for degenerate triangle (two identical vertices)
			if (tri.v[0] == tri.v[1] || tri.v[1] == tri.v[2] || tri.v[0] == tri.v[2])
			{
				triDeleted[fi] = true;
				--liveTris;
			}
			else
			{
				// Add this face to va's adjacency if not already there
				vertFaces[va].push_back(fi);
			}
		}

		// Remove duplicates from vertFaces[va]
		{
			auto &vf = vertFaces[va];
			std::sort(vf.begin(), vf.end());
			vf.erase(std::unique(vf.begin(), vf.end()), vf.end());
			// Remove deleted faces
			vf.erase(std::remove_if(vf.begin(), vf.end(),
				[&](int fi) { return triDeleted[fi]; }), vf.end());
		}

		// Update edges: remap vb -> va in all edges touching vb
		// and recompute costs for all edges touching va
		std::unordered_set<int> edgesToUpdate;

		for (int nei : vertEdges[vb])
		{
			auto &e = edges[nei];
			if (e.v0 == vb) e.v0 = va;
			if (e.v1 == vb) e.v1 = va;
			// Normalize order
			if (e.v0 > e.v1) std::swap(e.v0, e.v1);
			vertEdges[va].push_back(nei);
			edgesToUpdate.insert(nei);
		}
		for (int nei : vertEdges[va])
			edgesToUpdate.insert(nei);

		// Recompute costs and re-insert into PQ
		for (int nei : edgesToUpdate)
		{
			auto &e = edges[nei];
			// Skip self-loops or edges to deleted vertices
			if (e.v0 == e.v1 || vertDeleted[e.v0] || vertDeleted[e.v1])
				continue;
			auto [opt, cost] = computeEdgeCost(e.v0, e.v1);
			e.optimalPos = opt;
			e.cost = cost;
			e.gen++;
			pq.push({e.cost, nei, e.gen});
		}
	}

	// Step 6: Compact and build output mesh
	ObjMesh result;

	// Build vertex remapping (old -> new dense index)
	std::vector<int> vertMap(numVerts, -1);
	int newVertCount = 0;
	for (int i = 0; i < numVerts; ++i)
	{
		if (!vertDeleted[i])
		{
			vertMap[i] = newVertCount++;
			result.vertices.push_back(verts[i]);
		}
	}

	// Build faces with remapped indices
	for (int fi = 0; fi < numTris; ++fi)
	{
		if (triDeleted[fi]) continue;
		const auto &tri = triangles[fi];
		int a = vertMap[tri.v[0]];
		int b = vertMap[tri.v[1]];
		int c = vertMap[tri.v[2]];
		if (a < 0 || b < 0 || c < 0) continue;
		if (a == b || b == c || a == c) continue;
		result.faces.push_back(ObjMesh::Face{{a, b, c}});
	}

	result.buildEdges();
	return result;
}

} // namespace MeshDecimator
