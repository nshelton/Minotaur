#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

// Minimal OBJ loader: extracts vertex positions and face indices.
// Supports triangular and polygonal faces (fan-triangulated).
// No materials, normals, or texture coordinates.

struct ObjMesh
{
	struct Vec3 { float x, y, z; };
	struct Face { std::vector<int> indices; }; // 0-based vertex indices

	std::vector<Vec3> vertices;
	std::vector<Face> faces;

	// Edges deduplicated from faces (each edge stored once as sorted pair)
	struct Edge { int a, b; };
	std::vector<Edge> edges;

	bool empty() const { return vertices.empty(); }

	// Normalize: move centroid to origin, scale so average axis extent = 1
	void normalize()
	{
		if (vertices.empty()) return;

		// Compute centroid
		Vec3 centroid{0, 0, 0};
		for (const auto &v : vertices)
		{
			centroid.x += v.x;
			centroid.y += v.y;
			centroid.z += v.z;
		}
		float n = static_cast<float>(vertices.size());
		centroid.x /= n;
		centroid.y /= n;
		centroid.z /= n;

		// Translate to origin
		for (auto &v : vertices)
		{
			v.x -= centroid.x;
			v.y -= centroid.y;
			v.z -= centroid.z;
		}

		// Compute axis extents
		Vec3 lo{1e30f, 1e30f, 1e30f};
		Vec3 hi{-1e30f, -1e30f, -1e30f};
		for (const auto &v : vertices)
		{
			lo.x = std::min(lo.x, v.x); hi.x = std::max(hi.x, v.x);
			lo.y = std::min(lo.y, v.y); hi.y = std::max(hi.y, v.y);
			lo.z = std::min(lo.z, v.z); hi.z = std::max(hi.z, v.z);
		}

		float extX = hi.x - lo.x;
		float extY = hi.y - lo.y;
		float extZ = hi.z - lo.z;
		float avgExtent = (extX + extY + extZ) / 3.0f;
		if (avgExtent < 1e-9f) avgExtent = 1.0f;

		float scale = 1.0f / avgExtent;
		for (auto &v : vertices)
		{
			v.x *= scale;
			v.y *= scale;
			v.z *= scale;
		}
	}

	// Build deduplicated edge list from faces
	void buildEdges()
	{
		edges.clear();
		// Use a simple sorted-pair set via sorting + unique
		std::vector<std::pair<int,int>> edgeSet;
		edgeSet.reserve(faces.size() * 3);
		for (const auto &f : faces)
		{
			int nv = static_cast<int>(f.indices.size());
			for (int i = 0; i < nv; ++i)
			{
				int a = f.indices[i];
				int b = f.indices[(i + 1) % nv];
				if (a > b) std::swap(a, b);
				edgeSet.push_back({a, b});
			}
		}
		std::sort(edgeSet.begin(), edgeSet.end());
		edgeSet.erase(std::unique(edgeSet.begin(), edgeSet.end()), edgeSet.end());
		edges.reserve(edgeSet.size());
		for (const auto &[a, b] : edgeSet)
			edges.push_back(Edge{a, b});
	}

	// Load from .obj file. Returns true on success.
	static bool load(const std::string &path, ObjMesh &out, std::string *err = nullptr)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			if (err) *err = "Cannot open file: " + path;
			return false;
		}

		out.vertices.clear();
		out.faces.clear();
		out.edges.clear();

		std::string line;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#') continue;

			std::istringstream iss(line);
			std::string token;
			iss >> token;

			if (token == "v")
			{
				Vec3 v;
				if (iss >> v.x >> v.y >> v.z)
					out.vertices.push_back(v);
			}
			else if (token == "f")
			{
				Face face;
				std::string indexStr;
				while (iss >> indexStr)
				{
					// Handle v, v/vt, v/vt/vn, v//vn formats
					int vi = 0;
					std::sscanf(indexStr.c_str(), "%d", &vi);
					if (vi != 0)
					{
						// OBJ is 1-based; convert to 0-based
						// Negative indices are relative to current vertex count
						if (vi < 0)
							vi = static_cast<int>(out.vertices.size()) + vi;
						else
							vi -= 1;
						face.indices.push_back(vi);
					}
				}
				if (face.indices.size() >= 3)
					out.faces.push_back(std::move(face));
			}
		}

		if (out.vertices.empty())
		{
			if (err) *err = "No vertices found in OBJ file";
			return false;
		}

		out.normalize();
		out.buildEdges();
		return true;
	}
};
