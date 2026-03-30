#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"
#include "utils/ObjLoader.h"
#include "utils/MeshDecimator.h"
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
		m_parameters["mode"] = FilterParameter{
			"Mode", 0.0f, 3.0f, 0.0f,
			FilterParameter::Enum, {"Wireframe", "Isoline X", "Isoline Y", "Isoline Z"}
		};
		m_parameters["decimation"] = FilterParameter{"Decimation", 0.0f, 1.0f, 0.0f};
		m_parameters["hiddenLines"] = FilterParameter{
			"Hidden Line Removal", 0.0f, 1.0f, 0.0f, FilterParameter::Bool
		};
		m_parameters["depthBias"] = FilterParameter{"HLR Depth Bias", 0.0f, 0.01f, 0.005f};
		m_parameters["isoResolution"] = FilterParameter{"Iso Resolution", 10.0f, 100.0f, 30.0f};

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
		float decimation = m_parameters.at("decimation").value;

		// Apply QEM decimation in 3D before projection
		const ObjMesh &workMesh = (decimation > 0.0f)
			? (m_decimatedCache = MeshDecimator::decimate(m_mesh, decimation))
			: m_mesh;

		// Build rotation matrix (reuse the widget's Mat4 helpers)
		using M4 = MeshPreviewWidget::Mat4;
		M4 model = M4::rotationX(rx) * M4::rotationY(ry) * M4::rotationZ(rz);

		// Transform all vertices
		struct V3 { float x, y, z; };
		std::vector<V3> transformed(workMesh.vertices.size());
		for (size_t i = 0; i < workMesh.vertices.size(); ++i)
		{
			const auto &v = workMesh.vertices[i];
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

		// --- Isoline mode ---
		int modeIdx = static_cast<int>(m_parameters.at("mode").value + 0.5f);
		bool isolineMode = modeIdx >= 1 && modeIdx <= 3;
		if (isolineMode)
		{
			int axisIdx = modeIdx - 1; // 0=X, 1=Y, 2=Z
			int numSlices = static_cast<int>(m_parameters.at("isoResolution").value + 0.5f);

			// Helper to get the axis component from an object-space vertex
			auto getAxis = [&](const ObjMesh::Vec3 &v) -> float {
				if (axisIdx == 0) return v.x;
				if (axisIdx == 1) return v.y;
				return v.z;
			};

			// Transform an object-space point to view-space V3
			auto transformPt = [&](float ox, float oy, float oz) -> V3 {
				return {model.m[0]*ox + model.m[4]*oy + model.m[8]*oz,
				        model.m[1]*ox + model.m[5]*oy + model.m[9]*oz,
				        model.m[2]*ox + model.m[6]*oy + model.m[10]*oz};
			};

			// Find min/max along the chosen axis in object space
			float axisMin =  1e30f;
			float axisMax = -1e30f;
			for (const auto &v : workMesh.vertices)
			{
				float a = getAxis(v);
				axisMin = std::min(axisMin, a);
				axisMax = std::max(axisMax, a);
			}

			float axisRange = axisMax - axisMin;
			if (axisRange < 1e-6f) return;

			// --- Build depth buffer for HLR (shared with wireframe HLR below) ---
			constexpr float ISO_EPS = 1e-6f;
			auto cross2d = [](Vec2 a, Vec2 b) -> float {
				return a.x * b.y - a.y * b.x;
			};
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
				if (std::fabs(denom) < ISO_EPS)
					return {-1.0f, -1.0f, -1.0f};
				float bv = (d11 * d20 - d01 * d21) / denom;
				float bw = (d00 * d21 - d01 * d20) / denom;
				float bu = 1.0f - bv - bw;
				return {bu, bv, bw};
			};

			// Detect winding
			float signedVolume = 0.0f;
			for (const auto &face : workMesh.faces)
			{
				int nv = static_cast<int>(face.indices.size());
				if (nv < 3) continue;
				for (int i = 1; i < nv - 1; ++i)
				{
					const auto &a = workMesh.vertices[face.indices[0]];
					const auto &b = workMesh.vertices[face.indices[i]];
					const auto &c = workMesh.vertices[face.indices[i + 1]];
					signedVolume += a.x * (b.y * c.z - b.z * c.y)
					              + a.y * (b.z * c.x - b.x * c.z)
					              + a.z * (b.x * c.y - b.y * c.x);
				}
			}
			float windingSign = (signedVolume >= 0.0f) ? 1.0f : -1.0f;

			// Build front-facing projected triangles
			struct ProjTri { Vec2 p0, p1, p2; float z0, z1, z2; };
			std::vector<ProjTri> frontTris;
			frontTris.reserve(workMesh.faces.size());

			for (const auto &face : workMesh.faces)
			{
				int nv = static_cast<int>(face.indices.size());
				if (nv < 3) continue;
				for (int i = 1; i < nv - 1; ++i)
				{
					int i0 = face.indices[0], i1 = face.indices[i], i2 = face.indices[i + 1];
					const V3 &a = transformed[i0];
					const V3 &b = transformed[i1];
					const V3 &c = transformed[i2];

					float ex1 = b.x - a.x, ey1 = b.y - a.y;
					float ex2 = c.x - a.x, ey2 = c.y - a.y;
					float nz = (ex1 * ey2 - ey1 * ex2) * windingSign;
					if (nz <= 0.0f) continue;

					int fi0 = i0, fi1 = i1, fi2 = i2;
					if (windingSign < 0.0f) std::swap(fi1, fi2);

					Vec2 p0 = project(transformed[fi0]);
					Vec2 p1 = project(transformed[fi1]);
					Vec2 p2 = project(transformed[fi2]);

					float area2d = cross2d(p1 - p0, p2 - p0);
					if (std::fabs(area2d) < ISO_EPS) continue;

					ProjTri tri;
					tri.p0 = p0; tri.p1 = p1; tri.p2 = p2;
					tri.z0 = transformed[fi0].z;
					tri.z1 = transformed[fi1].z;
					tri.z2 = transformed[fi2].z;
					frontTris.push_back(tri);
				}
			}

			// Rasterize depth buffer
			constexpr int ISO_DBUF_SIZE = 1024;
			float projMinX = 1e30f, projMinY = 1e30f;
			float projMaxX = -1e30f, projMaxY = -1e30f;
			for (const auto &v : transformed)
			{
				Vec2 p = project(v);
				projMinX = std::min(projMinX, p.x);
				projMinY = std::min(projMinY, p.y);
				projMaxX = std::max(projMaxX, p.x);
				projMaxY = std::max(projMaxY, p.y);
			}
			float padX = (projMaxX - projMinX) * 0.05f;
			float padY = (projMaxY - projMinY) * 0.05f;
			projMinX -= padX; projMinY -= padY;
			projMaxX += padX; projMaxY += padY;
			float projW = projMaxX - projMinX;
			float projH = projMaxY - projMinY;
			if (projW < ISO_EPS || projH < ISO_EPS) return;

			auto toPixelX = [&](float x) -> int {
				return std::clamp(static_cast<int>((x - projMinX) / projW * (ISO_DBUF_SIZE - 1)),
					0, ISO_DBUF_SIZE - 1);
			};
			auto toPixelY = [&](float y) -> int {
				return std::clamp(static_cast<int>((y - projMinY) / projH * (ISO_DBUF_SIZE - 1)),
					0, ISO_DBUF_SIZE - 1);
			};
			auto fromPixelX = [&](int px) -> float {
				return projMinX + (px + 0.5f) / ISO_DBUF_SIZE * projW;
			};
			auto fromPixelY = [&](int py) -> float {
				return projMinY + (py + 0.5f) / ISO_DBUF_SIZE * projH;
			};

			std::vector<float> depthBuf(ISO_DBUF_SIZE * ISO_DBUF_SIZE, -1e30f);
			for (const auto &tri : frontTris)
			{
				int px0 = toPixelX(tri.p0.x), py0 = toPixelY(tri.p0.y);
				int px1 = toPixelX(tri.p1.x), py1 = toPixelY(tri.p1.y);
				int px2 = toPixelX(tri.p2.x), py2 = toPixelY(tri.p2.y);
				int minPx = std::min({px0, px1, px2}), maxPx = std::max({px0, px1, px2});
				int minPy = std::min({py0, py1, py2}), maxPy = std::max({py0, py1, py2});
				for (int py = minPy; py <= maxPy; ++py)
				{
					for (int px = minPx; px <= maxPx; ++px)
					{
						Vec2 pt{fromPixelX(px), fromPixelY(py)};
						auto [u, v, w] = barycentricCoords(pt, tri.p0, tri.p1, tri.p2);
						if (u >= 0.0f && v >= 0.0f && w >= 0.0f)
						{
							float z = u * tri.z0 + v * tri.z1 + w * tri.z2;
							int idx = py * ISO_DBUF_SIZE + px;
							if (z > depthBuf[idx]) depthBuf[idx] = z;
						}
					}
				}
			}

			float DEPTH_EPS = m_parameters.at("depthBias").value;
			constexpr int NUM_SAMPLES = 8;

			// For each slice plane, intersect with every triangle and collect segments
			for (int s = 1; s < numSlices; ++s)
			{
				float t = static_cast<float>(s) / numSlices;
				float planeVal = axisMin + t * axisRange;

				// Segment with view-space Z for depth testing
				struct Seg3 { Vec2 a, b; float za, zb; };
				std::vector<Seg3> segments;

				for (const auto &face : workMesh.faces)
				{
					int nv = static_cast<int>(face.indices.size());
					if (nv < 3) continue;

					for (int i = 1; i < nv - 1; ++i)
					{
						int i0 = face.indices[0];
						int i1 = face.indices[i];
						int i2 = face.indices[i + 1];

						const auto &ov0 = workMesh.vertices[i0];
						const auto &ov1 = workMesh.vertices[i1];
						const auto &ov2 = workMesh.vertices[i2];

						float a0 = getAxis(ov0);
						float a1 = getAxis(ov1);
						float a2 = getAxis(ov2);

						// Interpolate in object space, transform, return projected + Z
						struct ProjZ { Vec2 p; float z; };
						auto lerpTransformProject = [&](const ObjMesh::Vec3 &va, const ObjMesh::Vec3 &vb, float et) -> ProjZ {
							float ox = va.x + et * (vb.x - va.x);
							float oy = va.y + et * (vb.y - va.y);
							float oz = va.z + et * (vb.z - va.z);
							V3 tv = transformPt(ox, oy, oz);
							return {project(tv), tv.z};
						};
						auto vertexProject = [&](const ObjMesh::Vec3 &v) -> ProjZ {
							V3 tv = transformPt(v.x, v.y, v.z);
							return {project(tv), tv.z};
						};

						std::vector<ProjZ> hits;
						hits.reserve(2);

						if ((a0 - planeVal) * (a1 - planeVal) < 0.0f)
						{
							float et = (planeVal - a0) / (a1 - a0);
							hits.push_back(lerpTransformProject(ov0, ov1, et));
						}
						if ((a1 - planeVal) * (a2 - planeVal) < 0.0f)
						{
							float et = (planeVal - a1) / (a2 - a1);
							hits.push_back(lerpTransformProject(ov1, ov2, et));
						}
						if ((a2 - planeVal) * (a0 - planeVal) < 0.0f)
						{
							float et = (planeVal - a2) / (a0 - a2);
							hits.push_back(lerpTransformProject(ov2, ov0, et));
						}

						constexpr float VEPS = 1e-6f;
						if (std::fabs(a0 - planeVal) < VEPS) hits.push_back(vertexProject(ov0));
						if (std::fabs(a1 - planeVal) < VEPS) hits.push_back(vertexProject(ov1));
						if (std::fabs(a2 - planeVal) < VEPS) hits.push_back(vertexProject(ov2));

						if (hits.size() >= 2)
							segments.push_back({hits[0].p, hits[1].p, hits[0].z, hits[1].z});
					}
				}

				// Depth-test each segment: sample along the segment, skip if occluded
				std::vector<Seg3> visibleSegments;
				if (hlr)
				{
					for (const auto &seg : segments)
					{
						bool anyVisible = false;
						for (int ns = 0; ns < NUM_SAMPLES; ++ns)
						{
							float st = static_cast<float>(ns) / (NUM_SAMPLES - 1);
							float segZ = seg.za + st * (seg.zb - seg.za);
							Vec2 pt = seg.a + (seg.b - seg.a) * st;
							int px = toPixelX(pt.x);
							int py = toPixelY(pt.y);
							float bufZ = depthBuf[py * ISO_DBUF_SIZE + px];
							if (bufZ <= segZ + DEPTH_EPS)
							{
								anyVisible = true;
								break;
							}
						}
						if (anyVisible)
							visibleSegments.push_back(seg);
					}
				}
				else
				{
					visibleSegments = std::move(segments);
				}

				// Chain contiguous segments into polylines by matching endpoints
				constexpr float CHAIN_EPS = 1e-4f;
				auto dist2 = [](Vec2 a, Vec2 b) {
					float dx = a.x - b.x, dy = a.y - b.y;
					return dx * dx + dy * dy;
				};
				std::vector<bool> used(visibleSegments.size(), false);

				for (size_t si = 0; si < visibleSegments.size(); ++si)
				{
					if (used[si]) continue;
					used[si] = true;

					Path path;
					path.closed = false;
					path.points.push_back(visibleSegments[si].a);
					path.points.push_back(visibleSegments[si].b);

					bool grew = true;
					while (grew)
					{
						grew = false;
						for (size_t sj = 0; sj < visibleSegments.size(); ++sj)
						{
							if (used[sj]) continue;
							Vec2 back = path.points.back();
							Vec2 front = path.points.front();
							if (dist2(back, visibleSegments[sj].a) < CHAIN_EPS)
							{
								path.points.push_back(visibleSegments[sj].b);
								used[sj] = true; grew = true;
							}
							else if (dist2(back, visibleSegments[sj].b) < CHAIN_EPS)
							{
								path.points.push_back(visibleSegments[sj].a);
								used[sj] = true; grew = true;
							}
							else if (dist2(front, visibleSegments[sj].b) < CHAIN_EPS)
							{
								path.points.insert(path.points.begin(), visibleSegments[sj].a);
								used[sj] = true; grew = true;
							}
							else if (dist2(front, visibleSegments[sj].a) < CHAIN_EPS)
							{
								path.points.insert(path.points.begin(), visibleSegments[sj].b);
								used[sj] = true; grew = true;
							}
						}
					}

					out.paths.push_back(std::move(path));
				}
			}
			return;
		}

		// Simple path: no hidden line removal
		if (!hlr)
		{
			for (const auto &edge : workMesh.edges)
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

		// Step A: Build projected front-facing triangles
		struct ProjTri
		{
			Vec2 p0, p1, p2;
			float z0, z1, z2;
			int vi0, vi1, vi2;
			Vec2 bmin, bmax;
		};

		// Detect winding order via signed volume of the original mesh.
		// This is view-independent (pure rotation preserves handedness).
		float signedVolume = 0.0f;
		for (const auto &face : workMesh.faces)
		{
			int nv = static_cast<int>(face.indices.size());
			if (nv < 3) continue;
			for (int i = 1; i < nv - 1; ++i)
			{
				const auto &a = workMesh.vertices[face.indices[0]];
				const auto &b = workMesh.vertices[face.indices[i]];
				const auto &c = workMesh.vertices[face.indices[i + 1]];
				signedVolume += a.x * (b.y * c.z - b.z * c.y)
				              + a.y * (b.z * c.x - b.x * c.z)
				              + a.z * (b.x * c.y - b.y * c.x);
			}
		}
		// Positive signed volume = CCW outward normals; negative = CW
		float windingSign = (signedVolume >= 0.0f) ? 1.0f : -1.0f;

		std::vector<ProjTri> frontTris;
		frontTris.reserve(workMesh.faces.size());

		for (const auto &face : workMesh.faces)
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

		// Step B: Rasterize front-facing triangles into a software depth buffer
		constexpr int DBUF_SIZE = 1024;

		// Find projected bounding box of all vertices
		float projMinX =  1e30f, projMinY =  1e30f;
		float projMaxX = -1e30f, projMaxY = -1e30f;
		for (const auto &v : transformed)
		{
			Vec2 p = project(v);
			projMinX = std::min(projMinX, p.x);
			projMinY = std::min(projMinY, p.y);
			projMaxX = std::max(projMaxX, p.x);
			projMaxY = std::max(projMaxY, p.y);
		}
		float padX = (projMaxX - projMinX) * 0.05f;
		float padY = (projMaxY - projMinY) * 0.05f;
		projMinX -= padX; projMinY -= padY;
		projMaxX += padX; projMaxY += padY;
		float projW = projMaxX - projMinX;
		float projH = projMaxY - projMinY;
		if (projW < EPS || projH < EPS) return;

		// Map projected coords <-> pixel coords
		auto toPixelX = [&](float x) -> int {
			return std::clamp(static_cast<int>((x - projMinX) / projW * (DBUF_SIZE - 1)),
				0, DBUF_SIZE - 1);
		};
		auto toPixelY = [&](float y) -> int {
			return std::clamp(static_cast<int>((y - projMinY) / projH * (DBUF_SIZE - 1)),
				0, DBUF_SIZE - 1);
		};
		auto fromPixelX = [&](int px) -> float {
			return projMinX + (px + 0.5f) / DBUF_SIZE * projW;
		};
		auto fromPixelY = [&](int py) -> float {
			return projMinY + (py + 0.5f) / DBUF_SIZE * projH;
		};

		// Initialize depth buffer (higher z = closer to camera)
		std::vector<float> depthBuf(DBUF_SIZE * DBUF_SIZE, -1e30f);

		// Rasterize each front-facing triangle
		for (const auto &tri : frontTris)
		{
			int px0 = toPixelX(tri.p0.x), py0 = toPixelY(tri.p0.y);
			int px1 = toPixelX(tri.p1.x), py1 = toPixelY(tri.p1.y);
			int px2 = toPixelX(tri.p2.x), py2 = toPixelY(tri.p2.y);

			int minPx = std::min({px0, px1, px2});
			int maxPx = std::max({px0, px1, px2});
			int minPy = std::min({py0, py1, py2});
			int maxPy = std::max({py0, py1, py2});

			for (int py = minPy; py <= maxPy; ++py)
			{
				for (int px = minPx; px <= maxPx; ++px)
				{
					Vec2 pt{fromPixelX(px), fromPixelY(py)};
					auto [u, v, w] = barycentricCoords(pt, tri.p0, tri.p1, tri.p2);
					if (u >= 0.0f && v >= 0.0f && w >= 0.0f)
					{
						float z = u * tri.z0 + v * tri.z1 + w * tri.z2;
						int idx = py * DBUF_SIZE + px;
						if (z > depthBuf[idx]) depthBuf[idx] = z;
					}
				}
			}
		}

		// Step C: Test each edge against depth buffer with multiple samples.
		// If ANY sample is visible, emit the edge.
		float DEPTH_EPS = m_parameters.at("depthBias").value;
		constexpr int NUM_SAMPLES = 8;

		for (const auto &edge : workMesh.edges)
		{
			const V3 &va = transformed[edge.a];
			const V3 &vb = transformed[edge.b];
			Vec2 pa = project(va);
			Vec2 pb = project(vb);

			bool anyVisible = false;
			for (int s = 0; s < NUM_SAMPLES; ++s)
			{
				float t = static_cast<float>(s) / (NUM_SAMPLES - 1);
				float edgeZ = va.z + t * (vb.z - va.z);
				Vec2 pt = pa + (pb - pa) * t;
				int px = toPixelX(pt.x);
				int py = toPixelY(pt.y);
				float bufZ = depthBuf[py * DBUF_SIZE + px];
				if (bufZ <= edgeZ + DEPTH_EPS)
				{
					anyVisible = true;
					break;
				}
			}
			if (!anyVisible) continue;

			Path p;
			p.closed = false;
			p.points.push_back(pa);
			p.points.push_back(pb);
			out.paths.push_back(std::move(p));
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

	void onStringParameterChanged(const std::string &key) override
	{
		if (key == "file")
		{
			loadMesh(m_stringParameters["file"]);
		}
	}

private:
	ObjMesh m_mesh;
	mutable ObjMesh m_decimatedCache;
	mutable MeshPreviewWidget m_preview;
};
