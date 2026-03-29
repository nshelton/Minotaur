#pragma once

#include <cmath>
#include <string>
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

		// Build rotation matrix (reuse the widget's Mat4 helpers)
		using M4 = MeshPreviewWidget::Mat4;
		M4 model = M4::rotationX(rx) * M4::rotationY(ry) * M4::rotationZ(rz);

		// Transform all vertices
		struct V3 { float x, y, z; };
		std::vector<V3> transformed(m_mesh.vertices.size());
		for (size_t i = 0; i < m_mesh.vertices.size(); ++i)
		{
			const auto &v = m_mesh.vertices[i];
			// Apply rotation (just the 3x3 part of the 4x4)
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
				// Simple perspective: camera at z=3, fov~45 deg
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

		// Create one path per edge
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
