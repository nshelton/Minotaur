#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"
#include "utils/ObjLoader.h"
#include "utils/MeshDecimator.h"
#include "utils/MeshPreviewWidget.h"
#include "utils/DepthBufferGPU.h"
#include "utils/ParallelFor.h"

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

		// Apply QEM decimation in 3D before projection.
		// If no explicit decimation, use pre-built LOD for large meshes.
		const ObjMesh *workMeshPtr = &m_mesh;
		if (decimation > 0.0f)
		{
			m_decimatedCache = MeshDecimator::decimate(m_mesh, decimation);
			workMeshPtr = &m_decimatedCache;
		}
		else if (!m_lodLevels.empty())
		{
			// Use coarsest LOD for HLR (expensive), finer LOD for wireframe
			if (hlr && m_lodLevels.size() > 1)
				workMeshPtr = &m_lodLevels[1];
			else
				workMeshPtr = &m_lodLevels[0];
		}
		const ObjMesh &workMesh = *workMeshPtr;

		// Build rotation matrix (reuse the widget's Mat4 helpers)
		using M4 = MeshPreviewWidget::Mat4;
		M4 model = M4::rotationX(rx) * M4::rotationY(ry) * M4::rotationZ(rz);

		// Transform all vertices (parallel for large meshes)
		m_transformed.resize(workMesh.vertices.size());
		const float *mm = model.m;
		const auto &verts = workMesh.vertices;
		auto &xformed = m_transformed;
		parallel::parallel_for(0, verts.size(), [&](size_t i) {
			const auto &v = verts[i];
			xformed[i] = {
				mm[0]*v.x + mm[4]*v.y + mm[8]*v.z,
				mm[1]*v.x + mm[5]*v.y + mm[9]*v.z,
				mm[2]*v.x + mm[6]*v.y + mm[10]*v.z
			};
		});

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

			auto getAxis = [&](const ObjMesh::Vec3 &v) -> float {
				if (axisIdx == 0) return v.x;
				if (axisIdx == 1) return v.y;
				return v.z;
			};

			auto transformPt = [&](float ox, float oy, float oz) -> V3 {
				return {model.m[0]*ox + model.m[4]*oy + model.m[8]*oz,
				        model.m[1]*ox + model.m[5]*oy + model.m[9]*oz,
				        model.m[2]*ox + model.m[6]*oy + model.m[10]*oz};
			};

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

			// Build depth buffer for HLR using shared helpers
			float windingSign = computeWindingSign(workMesh);

			m_frontTris.clear();
			buildFrontFacingTris(workMesh, m_transformed, windingSign, project, m_frontTris);

			DepthBufContext dbCtx;
			computeProjectionBounds(m_transformed, project, dbCtx);
			if (dbCtx.projW < 1e-6f || dbCtx.projH < 1e-6f) return;

			rasterizeDepthBuffer(m_frontTris, dbCtx);

			float DEPTH_EPS = m_parameters.at("depthBias").value;

			// For each slice plane, intersect with every triangle and collect segments
			for (int s = 1; s < numSlices; ++s)
			{
				float t = static_cast<float>(s) / numSlices;
				float planeVal = axisMin + t * axisRange;

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

				// Depth-test each segment
				std::vector<Seg3> visibleSegments;
				if (hlr)
				{
					for (const auto &seg : segments)
					{
						if (isSegmentVisible(seg.a, seg.b, seg.za, seg.zb, dbCtx, DEPTH_EPS))
							visibleSegments.push_back(seg);
					}
				}
				else
				{
					visibleSegments = std::move(segments);
				}

				// Chain contiguous segments into polylines using spatial hashing
				chainSegments(visibleSegments, out);
			}
			return;
		}

		// Simple path: no hidden line removal
		if (!hlr)
		{
			for (const auto &edge : workMesh.edges)
			{
				Vec2 a = project(m_transformed[edge.a]);
				Vec2 b = project(m_transformed[edge.b]);
				Path p;
				p.closed = false;
				p.points.push_back(a);
				p.points.push_back(b);
				out.paths.push_back(std::move(p));
			}
			return;
		}

		// --- Hidden line removal (wireframe) ---
		float windingSign = computeWindingSign(workMesh);

		m_frontTris.clear();
		buildFrontFacingTris(workMesh, m_transformed, windingSign, project, m_frontTris);

		DepthBufContext dbCtx;
		computeProjectionBounds(m_transformed, project, dbCtx);
		if (dbCtx.projW < 1e-6f || dbCtx.projH < 1e-6f) return;

		rasterizeDepthBuffer(m_frontTris, dbCtx);

		float DEPTH_EPS = m_parameters.at("depthBias").value;

		// Parallel edge visibility testing
		std::vector<uint8_t> visible(workMesh.edges.size(), 0);
		parallel::parallel_for(0, workMesh.edges.size(), [&](size_t i) {
			const auto &edge = workMesh.edges[i];
			const V3 &va = m_transformed[edge.a];
			const V3 &vb = m_transformed[edge.b];
			Vec2 pa = project(va);
			Vec2 pb = project(vb);
			if (isSegmentVisible(pa, pb, va.z, vb.z, dbCtx, DEPTH_EPS))
				visible[i] = 1;
		});

		for (size_t i = 0; i < workMesh.edges.size(); ++i)
		{
			if (!visible[i]) continue;
			const auto &edge = workMesh.edges[i];
			Vec2 pa = project(m_transformed[edge.a]);
			Vec2 pb = project(m_transformed[edge.b]);
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
		m_lodBuilt = false;
		m_lodLevels.clear();
		buildLODs();
		m_version.fetch_add(1);
		return true;
	}

	void buildLODs() const
	{
		if (m_lodBuilt) return;
		m_lodBuilt = true;
		if (m_mesh.faces.size() < LOD_THRESHOLD) return;

		// Build 2 LOD levels: 75% decimation and 94% decimation
		m_lodLevels.resize(2);
		m_lodLevels[0] = MeshDecimator::decimate(m_mesh, 0.75f);
		m_lodLevels[1] = MeshDecimator::decimate(m_mesh, 0.94f);
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

	// Pre-computed LOD levels for large meshes
	static constexpr size_t LOD_THRESHOLD = 100000; // faces to trigger LOD
	mutable std::vector<ObjMesh> m_lodLevels;       // [0]=25%, [1]=6% of original
	mutable bool m_lodBuilt{false};

	// ── Shared HLR types and reusable buffers ──────────────────────────

	struct V3 { float x, y, z; };

	struct ProjTri
	{
		Vec2 p0, p1, p2;
		float z0, z1, z2;
	};

	static constexpr int DBUF_SIZE = 1024;
	static constexpr int NUM_SAMPLES = 8;
	static constexpr float EPS = 1e-6f;

	struct DepthBufContext
	{
		float projMinX, projMinY, projMaxX, projMaxY;
		float projW, projH;
		std::vector<float> depthBuf;

		int toPixelX(float x) const
		{
			return std::clamp(static_cast<int>((x - projMinX) / projW * (DBUF_SIZE - 1)),
				0, DBUF_SIZE - 1);
		}
		int toPixelY(float y) const
		{
			return std::clamp(static_cast<int>((y - projMinY) / projH * (DBUF_SIZE - 1)),
				0, DBUF_SIZE - 1);
		}
		float fromPixelX(int px) const
		{
			return projMinX + (px + 0.5f) / DBUF_SIZE * projW;
		}
		float fromPixelY(int py) const
		{
			return projMinY + (py + 0.5f) / DBUF_SIZE * projH;
		}
	};

	// Reusable buffers to avoid per-call allocation
	mutable std::vector<V3> m_transformed;
	mutable std::vector<ProjTri> m_frontTris;
	mutable DepthBufferGPU m_gpuDepth;

	// ── Shared HLR helper methods ──────────────────────────────────────

	static float computeWindingSign(const ObjMesh &mesh)
	{
		float signedVolume = 0.0f;
		for (const auto &face : mesh.faces)
		{
			int nv = static_cast<int>(face.indices.size());
			if (nv < 3) continue;
			for (int i = 1; i < nv - 1; ++i)
			{
				const auto &a = mesh.vertices[face.indices[0]];
				const auto &b = mesh.vertices[face.indices[i]];
				const auto &c = mesh.vertices[face.indices[i + 1]];
				signedVolume += a.x * (b.y * c.z - b.z * c.y)
				              + a.y * (b.z * c.x - b.x * c.z)
				              + a.z * (b.x * c.y - b.y * c.x);
			}
		}
		return (signedVolume >= 0.0f) ? 1.0f : -1.0f;
	}

	template <typename ProjectFn>
	static void buildFrontFacingTris(
		const ObjMesh &mesh,
		const std::vector<V3> &transformed,
		float windingSign,
		ProjectFn &&project,
		std::vector<ProjTri> &outTris)
	{
		outTris.reserve(mesh.faces.size());

		for (const auto &face : mesh.faces)
		{
			int nv = static_cast<int>(face.indices.size());
			if (nv < 3) continue;

			for (int i = 1; i < nv - 1; ++i)
			{
				int i0 = face.indices[0];
				int i1 = face.indices[i];
				int i2 = face.indices[i + 1];

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

				float area2d = (p1.x - p0.x) * (p2.y - p0.y)
				             - (p1.y - p0.y) * (p2.x - p0.x);
				if (std::fabs(area2d) < EPS) continue;

				outTris.push_back({p0, p1, p2,
					transformed[fi0].z, transformed[fi1].z, transformed[fi2].z});
			}
		}
	}

	template <typename ProjectFn>
	static void computeProjectionBounds(
		const std::vector<V3> &transformed,
		ProjectFn &&project,
		DepthBufContext &ctx)
	{
		ctx.projMinX =  1e30f; ctx.projMinY =  1e30f;
		ctx.projMaxX = -1e30f; ctx.projMaxY = -1e30f;
		for (const auto &v : transformed)
		{
			Vec2 p = project(v);
			ctx.projMinX = std::min(ctx.projMinX, p.x);
			ctx.projMinY = std::min(ctx.projMinY, p.y);
			ctx.projMaxX = std::max(ctx.projMaxX, p.x);
			ctx.projMaxY = std::max(ctx.projMaxY, p.y);
		}
		float padX = (ctx.projMaxX - ctx.projMinX) * 0.05f;
		float padY = (ctx.projMaxY - ctx.projMinY) * 0.05f;
		ctx.projMinX -= padX; ctx.projMinY -= padY;
		ctx.projMaxX += padX; ctx.projMaxY += padY;
		ctx.projW = ctx.projMaxX - ctx.projMinX;
		ctx.projH = ctx.projMaxY - ctx.projMinY;
	}

	static std::tuple<float, float, float> barycentricCoords(
		Vec2 p, Vec2 a, Vec2 b, Vec2 c)
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
		float bv = (d11 * d20 - d01 * d21) / denom;
		float bw = (d00 * d21 - d01 * d20) / denom;
		float bu = 1.0f - bv - bw;
		return {bu, bv, bw};
	}

	// Try GPU-accelerated depth buffer first, fall back to software rasterization
	void rasterizeDepthBuffer(
		const std::vector<ProjTri> &frontTris,
		DepthBufContext &ctx) const
	{
		if (rasterizeDepthBufferGPU(frontTris, ctx))
			return;
		rasterizeDepthBufferSoftware(frontTris, ctx);
	}

	bool rasterizeDepthBufferGPU(
		const std::vector<ProjTri> &frontTris,
		DepthBufContext &ctx) const
	{
		// Build triangle vertex array for GPU
		std::vector<DepthBufferGPU::TriVert> triVerts;
		triVerts.reserve(frontTris.size() * 3);

		float zMin =  1e30f, zMax = -1e30f;
		for (const auto &tri : frontTris)
		{
			zMin = std::min({zMin, tri.z0, tri.z1, tri.z2});
			zMax = std::max({zMax, tri.z0, tri.z1, tri.z2});
		}
		float zPad = (zMax - zMin) * 0.01f;
		if (zPad < 1e-6f) zPad = 0.01f;
		zMin -= zPad;
		zMax += zPad;

		for (const auto &tri : frontTris)
		{
			triVerts.push_back({tri.p0.x, tri.p0.y, tri.z0});
			triVerts.push_back({tri.p1.x, tri.p1.y, tri.z1});
			triVerts.push_back({tri.p2.x, tri.p2.y, tri.z2});
		}

		if (!m_gpuDepth.render(triVerts, ctx.projMinX, ctx.projMinY,
		                       ctx.projMaxX, ctx.projMaxY, zMin, zMax))
			return false;

		// Convert GPU depth buffer (GL depth [0,1]) back to view-space Z
		const auto &gpuData = m_gpuDepth.depthData();
		ctx.depthBuf.resize(DBUF_SIZE * DBUF_SIZE);
		for (int i = 0; i < DBUF_SIZE * DBUF_SIZE; ++i)
		{
			float glDepth = gpuData[i];
			if (glDepth >= 1.0f)
				ctx.depthBuf[i] = -1e30f; // no triangle here
			else
				ctx.depthBuf[i] = m_gpuDepth.depthToViewZ(glDepth);
		}
		return true;
	}

	// Scanline rasterizer: walks only pixels inside each triangle.
	// Uses edge walking with incremental Z interpolation — no per-pixel
	// barycentric computation needed. ~2-4x faster than bbox + bary test.
	static void rasterizeDepthBufferSoftware(
		const std::vector<ProjTri> &frontTris,
		DepthBufContext &ctx)
	{
		ctx.depthBuf.resize(DBUF_SIZE * DBUF_SIZE);
		std::fill(ctx.depthBuf.begin(), ctx.depthBuf.end(), -1e30f);

		for (const auto &tri : frontTris)
		{
			// Convert to pixel coordinates (as floats for sub-pixel precision)
			float fx0 = (tri.p0.x - ctx.projMinX) / ctx.projW * (DBUF_SIZE - 1);
			float fy0 = (tri.p0.y - ctx.projMinY) / ctx.projH * (DBUF_SIZE - 1);
			float fx1 = (tri.p1.x - ctx.projMinX) / ctx.projW * (DBUF_SIZE - 1);
			float fy1 = (tri.p1.y - ctx.projMinY) / ctx.projH * (DBUF_SIZE - 1);
			float fx2 = (tri.p2.x - ctx.projMinX) / ctx.projW * (DBUF_SIZE - 1);
			float fy2 = (tri.p2.y - ctx.projMinY) / ctx.projH * (DBUF_SIZE - 1);

			// Sort vertices by Y (v0.y <= v1.y <= v2.y)
			float vx[3] = {fx0, fx1, fx2};
			float vy[3] = {fy0, fy1, fy2};
			float vz[3] = {tri.z0, tri.z1, tri.z2};

			// Bubble sort 3 elements by y
			if (vy[0] > vy[1]) { std::swap(vx[0], vx[1]); std::swap(vy[0], vy[1]); std::swap(vz[0], vz[1]); }
			if (vy[1] > vy[2]) { std::swap(vx[1], vx[2]); std::swap(vy[1], vy[2]); std::swap(vz[1], vz[2]); }
			if (vy[0] > vy[1]) { std::swap(vx[0], vx[1]); std::swap(vy[0], vy[1]); std::swap(vz[0], vz[1]); }

			int yMin = std::max(0, static_cast<int>(std::ceil(vy[0])));
			int yMid = std::clamp(static_cast<int>(std::ceil(vy[1])), 0, DBUF_SIZE - 1);
			int yMax = std::min(DBUF_SIZE - 1, static_cast<int>(std::floor(vy[2])));

			if (yMin > yMax) continue;

			// Precompute edge slopes
			float dy02 = vy[2] - vy[0];
			float dy01 = vy[1] - vy[0];
			float dy12 = vy[2] - vy[1];

			if (dy02 < 1e-6f) continue; // degenerate

			float invDy02 = 1.0f / dy02;
			float dxdy02 = (vx[2] - vx[0]) * invDy02;
			float dzdy02 = (vz[2] - vz[0]) * invDy02;

			// Top half: v0 to v1
			if (dy01 > 1e-6f)
			{
				float invDy01 = 1.0f / dy01;
				float dxdy01 = (vx[1] - vx[0]) * invDy01;
				float dzdy01 = (vz[1] - vz[0]) * invDy01;

				for (int py = yMin; py < yMid && py <= yMax; ++py)
				{
					float t = static_cast<float>(py) - vy[0];
					float xA = vx[0] + t * dxdy02;
					float zA = vz[0] + t * dzdy02;
					float xB = vx[0] + t * dxdy01;
					float zB = vz[0] + t * dzdy01;

					if (xA > xB) { std::swap(xA, xB); std::swap(zA, zB); }

					int pxMin = std::max(0, static_cast<int>(std::ceil(xA)));
					int pxMax = std::min(DBUF_SIZE - 1, static_cast<int>(std::floor(xB)));
					if (pxMin > pxMax) continue;

					float span = xB - xA;
					float dzdx = (span > 1e-6f) ? (zB - zA) / span : 0.0f;
					float z = zA + (pxMin - xA) * dzdx;

					float *row = &ctx.depthBuf[py * DBUF_SIZE];
					for (int px = pxMin; px <= pxMax; ++px, z += dzdx)
					{
						if (z > row[px]) row[px] = z;
					}
				}
			}

			// Bottom half: v1 to v2
			if (dy12 > 1e-6f)
			{
				float invDy12 = 1.0f / dy12;
				float dxdy12 = (vx[2] - vx[1]) * invDy12;
				float dzdy12 = (vz[2] - vz[1]) * invDy12;

				int startY = std::max(yMin, yMid);
				for (int py = startY; py <= yMax; ++py)
				{
					float t02 = static_cast<float>(py) - vy[0];
					float xA = vx[0] + t02 * dxdy02;
					float zA = vz[0] + t02 * dzdy02;

					float t12 = static_cast<float>(py) - vy[1];
					float xB = vx[1] + t12 * dxdy12;
					float zB = vz[1] + t12 * dzdy12;

					if (xA > xB) { std::swap(xA, xB); std::swap(zA, zB); }

					int pxMin = std::max(0, static_cast<int>(std::ceil(xA)));
					int pxMax = std::min(DBUF_SIZE - 1, static_cast<int>(std::floor(xB)));
					if (pxMin > pxMax) continue;

					float span = xB - xA;
					float dzdx = (span > 1e-6f) ? (zB - zA) / span : 0.0f;
					float z = zA + (pxMin - xA) * dzdx;

					float *row = &ctx.depthBuf[py * DBUF_SIZE];
					for (int px = pxMin; px <= pxMax; ++px, z += dzdx)
					{
						if (z > row[px]) row[px] = z;
					}
				}
			}
		}
	}

	static bool isSegmentVisible(
		Vec2 pa, Vec2 pb, float za, float zb,
		const DepthBufContext &ctx, float depthEps)
	{
		for (int s = 0; s < NUM_SAMPLES; ++s)
		{
			float t = static_cast<float>(s) / (NUM_SAMPLES - 1);
			float segZ = za + t * (zb - za);
			Vec2 pt = pa + (pb - pa) * t;
			int px = ctx.toPixelX(pt.x);
			int py = ctx.toPixelY(pt.y);
			float bufZ = ctx.depthBuf[py * DBUF_SIZE + px];
			if (bufZ <= segZ + depthEps)
				return true;
		}
		return false;
	}

	// Chain contiguous segments into polylines using spatial hashing
	// instead of O(n^2) nested scanning
	template <typename SegType>
	static void chainSegments(const std::vector<SegType> &segments, PathSet &out)
	{
		if (segments.empty()) return;

		constexpr float CHAIN_EPS = 1e-4f;
		constexpr float INV_EPS = 1.0f / CHAIN_EPS;

		auto quantize = [&](Vec2 p) -> int64_t {
			int32_t qx = static_cast<int32_t>(std::round(p.x * INV_EPS));
			int32_t qy = static_cast<int32_t>(std::round(p.y * INV_EPS));
			return (static_cast<int64_t>(qx) << 32) | static_cast<int64_t>(static_cast<uint32_t>(qy));
		};

		// Build spatial hash: endpoint -> list of (segment index, which end: 0=a, 1=b)
		std::unordered_map<int64_t, std::vector<std::pair<int, int>>> endpointMap;
		endpointMap.reserve(segments.size() * 2);
		for (size_t i = 0; i < segments.size(); ++i)
		{
			endpointMap[quantize(segments[i].a)].push_back({static_cast<int>(i), 0});
			endpointMap[quantize(segments[i].b)].push_back({static_cast<int>(i), 1});
		}

		std::vector<bool> used(segments.size(), false);

		for (size_t si = 0; si < segments.size(); ++si)
		{
			if (used[si]) continue;
			used[si] = true;

			std::deque<Vec2> chain;
			chain.push_back(segments[si].a);
			chain.push_back(segments[si].b);

			// Grow from back
			bool grew = true;
			while (grew)
			{
				grew = false;
				int64_t key = quantize(chain.back());
				auto it = endpointMap.find(key);
				if (it == endpointMap.end()) continue;
				for (auto &[idx, end] : it->second)
				{
					if (used[idx]) continue;
					used[idx] = true;
					grew = true;
					if (end == 0)
						chain.push_back(segments[idx].b);
					else
						chain.push_back(segments[idx].a);
					break;
				}
			}

			// Grow from front
			grew = true;
			while (grew)
			{
				grew = false;
				int64_t key = quantize(chain.front());
				auto it = endpointMap.find(key);
				if (it == endpointMap.end()) continue;
				for (auto &[idx, end] : it->second)
				{
					if (used[idx]) continue;
					used[idx] = true;
					grew = true;
					if (end == 1)
						chain.push_front(segments[idx].a);
					else
						chain.push_front(segments[idx].b);
					break;
				}
			}

			Path path;
			path.closed = false;
			path.points.assign(chain.begin(), chain.end());
			out.paths.push_back(std::move(path));
		}
	}
};
