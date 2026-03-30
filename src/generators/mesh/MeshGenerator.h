#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"
#include "utils/ObjLoader.h"
#include "utils/MeshPreviewWidget.h"

// Generates a PathSet by projecting a 3D OBJ mesh onto 2D.
//
// Parameters:
//   rotX, rotY, rotZ  - Euler rotation in degrees
//   scale_mm          - output size in millimeters
//   projection        - 0 = Orthographic, 1 = Perspective
//
// String parameters:
//   file              - path to the .obj file
//
// The mesh is automatically normalized on load (centered at origin, scaled
// to unit average extent). The scale_mm parameter then maps the unit cube
// to mm coordinates for the page.
//
// The MeshPreviewWidget is owned here so the GUI panel can access it for
// the inline 3D preview without MeshGenerator knowing about ImGui.

struct MeshGenerator : public GeneratorTyped<MeshGenerator, PathSet>
{
	MeshGenerator()
	{
		m_parameters["rotX"]     = FilterParameter{"Rot X", -180.0f, 180.0f, -30.0f};
		m_parameters["rotY"]     = FilterParameter{"Rot Y", -180.0f, 180.0f, 45.0f};
		m_parameters["rotZ"]     = FilterParameter{"Rot Z", -180.0f, 180.0f, 0.0f};
		m_parameters["scale_mm"] = FilterParameter{"Scale (mm)", 1.0f, 300.0f, 80.0f};
		m_parameters["projection"] = FilterParameter{
			"Projection", 0.0f, 1.0f, 0.0f,
			FilterParameter::Enum, {"Orthographic", "Perspective"}
		};
		m_parameters["decimation"] = FilterParameter{"Decimation", 0.0f, 1.0f, 0.0f};
		m_parameters["hiddenLines"] = FilterParameter{
			"Hidden Line Removal", 0.0f, 1.0f, 0.0f, FilterParameter::Bool
		};

		m_stringParameters["file"] = "";
	}

	explicit MeshGenerator(const std::string &objPath)
		: MeshGenerator()
	{
		m_stringParameters["file"] = objPath;
		loadMesh(objPath);
	}

	const char *name() const override { return "Mesh"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	// Access for GUI to render the inline 3D preview
	MeshPreviewWidget &previewWidget() { return m_preview; }
	const ObjMesh &mesh() const { return m_mesh; }
	bool hasMesh() const { return !m_mesh.empty(); }

	void generateTyped(PathSet &out) const override
	{
		out.paths.clear();
		if (m_mesh.empty()) return;

		float rx = m_parameters.at("rotX").value;
		float ry = m_parameters.at("rotY").value;
		float rz = m_parameters.at("rotZ").value;
		float scaleMm = m_parameters.at("scale_mm").value;
		bool perspective = m_parameters.at("projection").value > 0.5f;
		bool hlr = m_parameters.at("hiddenLines").value > 0.5f;

		// Build rotation matrix (reuse the widget's Mat4 helpers)
		using M4 = MeshPreviewWidget::Mat4;
		M4 model = M4::rotationX(rx) * M4::rotationY(ry) * M4::rotationZ(rz);

		// Transform all vertices
		struct V3 { float x, y, z; };
		std::vector<V3> transformed(m_mesh.vertices.size());
		for (size_t i = 0; i < m_mesh.vertices.size(); ++i)
		{
			const auto &v = m_mesh.vertices[i];
			float ox = model.m[0]*v.x + model.m[4]*v.y + model.m[8]*v.z;
			float oy = model.m[1]*v.x + model.m[5]*v.y + model.m[9]*v.z;
			float oz = model.m[2]*v.x + model.m[6]*v.y + model.m[10]*v.z;
			transformed[i] = {ox, oy, oz};
		}

		// Project to 2D
		auto project = [&](const V3 &v) -> Vec2
		{
			if (perspective)
			{
				float d = 3.0f - v.z;
				if (d < 0.1f) d = 0.1f;
				float f = 2.0f / d;
				return Vec2(v.x * f * scaleMm, v.y * f * scaleMm);
			}
			else
			{
				return Vec2(v.x * scaleMm, v.y * scaleMm);
			}
		};

		// Decimation: sort edges by projected 2D length, skip shortest ones
		float decimation = m_parameters.at("decimation").value;
		int totalEdges = static_cast<int>(m_mesh.edges.size());
		int skipCount = static_cast<int>(decimation * totalEdges);

		// Build index + length pairs for sorting
		std::vector<std::pair<float, int>> edgeLengths(totalEdges);
		for (int i = 0; i < totalEdges; ++i)
		{
			Vec2 a = project(transformed[m_mesh.edges[i].a]);
			Vec2 b = project(transformed[m_mesh.edges[i].b]);
			float dx = b.x - a.x, dy = b.y - a.y;
			edgeLengths[i] = {dx * dx + dy * dy, i};
		}
		std::sort(edgeLengths.begin(), edgeLengths.end());

		// Output edges, skipping the shortest ones
		for (int i = skipCount; i < totalEdges; ++i)
		{
			const auto &edge = m_mesh.edges[edgeLengths[i].second];
			Vec2 a = project(transformed[edge.a]);
			Vec2 b = project(transformed[edge.b]);
			Path p;
			p.closed = false;
			p.points.push_back(a);
			p.points.push_back(b);
			out.paths.push_back(std::move(p));

    // Simple path: no hidden line removal
		if (!hlr)
		{
			for (const auto &edge : m_mesh.edges)
			{
				Vec2 a = project(transformed[edge.a]);
				Vec2 b = project(transformed[edge.b]);
				Path p;
				p.closed = false;
				p.points.push_back(a);
				p.points.push_back(b);
				out.paths.push_back(std::move(p));
			}
			return;
		}

		// --- Hidden line removal ---

		constexpr float EPS = 1e-6f;

		// 2D cross product (perp dot)
		auto cross2d = [](Vec2 a, Vec2 b) -> float
		{
			return a.x * b.y - a.y * b.x;
		};

		// Barycentric coordinates of p in triangle (a,b,c)
		auto barycentricCoords = [&](Vec2 p, Vec2 a, Vec2 b, Vec2 c)
			-> std::tuple<float, float, float>
		{
			Vec2 v0 = b - a, v1 = c - a, v2 = p - a;
			float d00 = v0.x * v0.x + v0.y * v0.y;
			float d01 = v0.x * v1.x + v0.y * v1.y;
			float d11 = v1.x * v1.x + v1.y * v1.y;
			float d20 = v2.x * v0.x + v2.y * v0.y;
			float d21 = v2.x * v1.x + v2.y * v1.y;
			float denom = d00 * d11 - d01 * d01;
			if (std::fabs(denom) < EPS)
				return {-1.0f, -1.0f, -1.0f};
			float v = (d11 * d20 - d01 * d21) / denom;
			float w = (d00 * d21 - d01 * d20) / denom;
			float u = 1.0f - v - w;
			return {u, v, w};
		};

		// Cyrus-Beck clip of parametric segment [0,1] against triangle half-planes.
		// Returns the sub-interval [tEnter, tExit] inside the triangle, or empty.
		auto clipSegmentToTriangle = [&](Vec2 pa, Vec2 pb,
			Vec2 t0, Vec2 t1, Vec2 t2)
			-> std::pair<float, float>
		{
			float tEnter = 0.0f, tExit = 1.0f;
			Vec2 d = pb - pa;

			// Three edges of the triangle: (t0,t1), (t1,t2), (t2,t0)
			Vec2 triVerts[3] = {t0, t1, t2};
			for (int i = 0; i < 3; ++i)
			{
				Vec2 e0 = triVerts[i];
				Vec2 e1 = triVerts[(i + 1) % 3];
				Vec2 edgeDir = e1 - e0;
				// Outward normal (right perpendicular for CCW triangle)
				Vec2 n{edgeDir.y, -edgeDir.x};

				Vec2 w = pa - e0;
				float num = -(n.x * w.x + n.y * w.y);
				float den = n.x * d.x + n.y * d.y;

				if (std::fabs(den) < EPS)
				{
					// Segment parallel to this edge
					if (num < 0.0f) return {1.0f, 0.0f}; // outside
					continue; // inside this half-plane
				}

				float t = num / den;
				if (den < 0.0f)
				{
					// Entering
					if (t > tEnter) tEnter = t;
				}
				else
				{
					// Exiting
					if (t < tExit) tExit = t;
				}
				if (tEnter > tExit) return {1.0f, 0.0f}; // empty
			}
			return {tEnter, tExit};
		};

		// Subtract interval [lo,hi] from a list of intervals
		auto subtractInterval = [](
			std::vector<std::pair<float,float>> &intervals,
			float lo, float hi)
		{
			if (lo >= hi) return;
			std::vector<std::pair<float,float>> result;
			result.reserve(intervals.size() + 1);
			for (auto &iv : intervals)
			{
				if (iv.second <= lo || iv.first >= hi)
				{
					// No overlap
					result.push_back(iv);
				}
				else
				{
					// Partial overlap - emit surviving portions
					if (iv.first < lo)
						result.push_back({iv.first, lo});
					if (iv.second > hi)
						result.push_back({hi, iv.second});
				}
			}
			intervals = std::move(result);
		};

		// Step A: Build projected front-facing triangles
		struct ProjTri
		{
			Vec2 p0, p1, p2;
			float z0, z1, z2;
			int vi0, vi1, vi2;
			Vec2 bmin, bmax;
		};

		// Auto-detect winding order: count which sign convention
		// produces more front-facing triangles
		int posFacing = 0, negFacing = 0;
		for (const auto &face : m_mesh.faces)
		{
			int nv = static_cast<int>(face.indices.size());
			if (nv < 3) continue;
			const V3 &a = transformed[face.indices[0]];
			const V3 &b = transformed[face.indices[1]];
			const V3 &c = transformed[face.indices[2]];
			float ex1 = b.x - a.x, ey1 = b.y - a.y;
			float ex2 = c.x - a.x, ey2 = c.y - a.y;
			float nz = ex1 * ey2 - ey1 * ex2;
			if (nz > 0.0f) ++posFacing;
			else if (nz < 0.0f) ++negFacing;
		}
		// If more faces have negative nz, the mesh uses CW winding
		float windingSign = (negFacing > posFacing) ? -1.0f : 1.0f;

		std::vector<ProjTri> frontTris;
		frontTris.reserve(m_mesh.faces.size());

		for (const auto &face : m_mesh.faces)
		{
			int nv = static_cast<int>(face.indices.size());
			if (nv < 3) continue;

			// Fan-triangulate the face
			for (int i = 1; i < nv - 1; ++i)
			{
				int i0 = face.indices[0];
				int i1 = face.indices[i];
				int i2 = face.indices[i + 1];

				const V3 &a = transformed[i0];
				const V3 &b = transformed[i1];
				const V3 &c = transformed[i2];

				// Camera-space face normal z-component
				float ex1 = b.x - a.x, ey1 = b.y - a.y;
				float ex2 = c.x - a.x, ey2 = c.y - a.y;
				float nz = (ex1 * ey2 - ey1 * ex2) * windingSign;

				if (nz <= 0.0f) continue; // back-facing

				// If winding was flipped, swap two vertices so the projected
				// triangle is CCW (required for Cyrus-Beck clipping)
				int fi0 = i0, fi1 = i1, fi2 = i2;
				if (windingSign < 0.0f) std::swap(fi1, fi2);

				Vec2 p0 = project(transformed[fi0]);
				Vec2 p1 = project(transformed[fi1]);
				Vec2 p2 = project(transformed[fi2]);

				// Degenerate triangle check in 2D
				float area2d = cross2d(p1 - p0, p2 - p0);
				if (std::fabs(area2d) < EPS) continue;

				ProjTri tri;
				tri.p0 = p0; tri.p1 = p1; tri.p2 = p2;
				tri.z0 = transformed[fi0].z;
				tri.z1 = transformed[fi1].z;
				tri.z2 = transformed[fi2].z;
				tri.vi0 = fi0; tri.vi1 = fi1; tri.vi2 = fi2;

				// 2D bounding box
				tri.bmin.x = std::min({p0.x, p1.x, p2.x});
				tri.bmin.y = std::min({p0.y, p1.y, p2.y});
				tri.bmax.x = std::max({p0.x, p1.x, p2.x});
				tri.bmax.y = std::max({p0.y, p1.y, p2.y});

				frontTris.push_back(tri);
			}
		}

		// Step B: For each edge, compute visible intervals after occlusion
		for (const auto &edge : m_mesh.edges)
		{
			const V3 &va = transformed[edge.a];
			const V3 &vb = transformed[edge.b];
			Vec2 pa = project(va);
			Vec2 pb = project(vb);

			// Edge bounding box
			float eMinX = std::min(pa.x, pb.x);
			float eMinY = std::min(pa.y, pb.y);
			float eMaxX = std::max(pa.x, pb.x);
			float eMaxY = std::max(pa.y, pb.y);

			std::vector<std::pair<float,float>> intervals;
			intervals.push_back({0.0f, 1.0f});

			for (const auto &tri : frontTris)
			{
				if (intervals.empty()) break;

				// Skip triangles that share a vertex with this edge
				if (tri.vi0 == edge.a || tri.vi0 == edge.b ||
				    tri.vi1 == edge.a || tri.vi1 == edge.b ||
				    tri.vi2 == edge.a || tri.vi2 == edge.b)
					continue;

				// Bounding box early rejection
				if (eMaxX < tri.bmin.x || eMinX > tri.bmax.x ||
				    eMaxY < tri.bmin.y || eMinY > tri.bmax.y)
					continue;

				// Clip edge segment to triangle in 2D
				auto [tEnter, tExit] = clipSegmentToTriangle(
					pa, pb, tri.p0, tri.p1, tri.p2);
				if (tEnter >= tExit) continue;

				// Depth test: interpolate edge z and triangle z at tEnter and tExit
				float edgeZ0 = va.z + tEnter * (vb.z - va.z);
				float edgeZ1 = va.z + tExit  * (vb.z - va.z);

				// Triangle z via barycentric interpolation at each endpoint
				Vec2 ptEnter = pa + (pb - pa) * tEnter;
				Vec2 ptExit  = pa + (pb - pa) * tExit;

				auto [u0, v0, w0] = barycentricCoords(ptEnter,
					tri.p0, tri.p1, tri.p2);
				auto [u1, v1, w1] = barycentricCoords(ptExit,
					tri.p0, tri.p1, tri.p2);

				float triZ0 = u0 * tri.z0 + v0 * tri.z1 + w0 * tri.z2;
				float triZ1 = u1 * tri.z0 + v1 * tri.z1 + w1 * tri.z2;

				// Triangle occludes where triZ > edgeZ (closer to camera = higher z)
				constexpr float DEPTH_EPS = 1e-4f;
				float diff0 = triZ0 - edgeZ0;
				float diff1 = triZ1 - edgeZ1;

				float occStart, occEnd;

				if (diff0 > DEPTH_EPS && diff1 > DEPTH_EPS)
				{
					// Entire overlap is occluded
					occStart = tEnter;
					occEnd = tExit;
				}
				else if (diff0 <= DEPTH_EPS && diff1 <= DEPTH_EPS)
				{
					// Triangle is behind the edge throughout - no occlusion
					continue;
				}
				else
				{
					// Depth crossover: find the t where depths are equal
					// diff(t) = diff0 + (diff1 - diff0) * (t - tEnter) / (tExit - tEnter)
					float tCross = tEnter + (tExit - tEnter) *
						(-diff0 + DEPTH_EPS) / (diff1 - diff0);
					tCross = std::max(tEnter, std::min(tExit, tCross));

					if (diff0 > DEPTH_EPS)
					{
						// Occluded from tEnter to tCross
						occStart = tEnter;
						occEnd = tCross;
					}
					else
					{
						// Occluded from tCross to tExit
						occStart = tCross;
						occEnd = tExit;
					}
				}

				subtractInterval(intervals, occStart, occEnd);
			}

			// Emit surviving visible segments as paths
			for (auto &[t0, t1] : intervals)
			{
				if (t1 - t0 < EPS) continue;
				Vec2 startPt = pa + (pb - pa) * t0;
				Vec2 endPt   = pa + (pb - pa) * t1;
				Path p;
				p.closed = false;
				p.points.push_back(startPt);
				p.points.push_back(endPt);
				out.paths.push_back(std::move(p));
			}
		}
	}

	std::unique_ptr<GeneratorBase> clone() const override
	{
		auto copy = std::make_unique<MeshGenerator>();
		for (const auto &kv : m_parameters)
			copy->setParameter(kv.first, kv.second.value);
		for (const auto &kv : m_stringParameters)
			copy->setStringParameter(kv.first, kv.second);
		// Deep copy mesh data
		if (!m_mesh.empty())
		{
			copy->m_mesh = m_mesh;
		}
		return copy;
	}

	// Load (or reload) mesh from file. Called when string parameter changes.
	bool loadMesh(const std::string &path)
	{
		if (path.empty()) return false;
		std::string err;
		if (!ObjMesh::load(path, m_mesh, &err))
		{
			m_mesh = ObjMesh{}; // clear on failure
			return false;
		}
		m_preview.setMesh(&m_mesh);
		m_version.fetch_add(1);
		return true;
	}

	// Called by GUI when the file path string parameter changes
	void onStringParameterChanged(const std::string &key)
	{
		if (key == "file")
		{
			loadMesh(m_stringParameters["file"]);
		}
	}

private:
	ObjMesh m_mesh;
	mutable MeshPreviewWidget m_preview;
};
