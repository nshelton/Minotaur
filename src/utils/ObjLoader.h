#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Minimal OBJ loader optimized for large meshes (millions of triangles).
// Extracts vertex positions and face indices only.
// No materials, normals, or texture coordinates.
//
// Optimizations over a naive loader:
//   - Memory-mapped file I/O (no std::ifstream overhead)
//   - Direct character-level parsing (no istringstream per line)
//   - Hash-based edge deduplication O(E) instead of O(E log E) sort
//   - Single-pass normalize (centroid + bounds computed together)

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

		// Single pass: compute centroid and bounds simultaneously
		double cx = 0, cy = 0, cz = 0;
		for (const auto &v : vertices)
		{
			cx += v.x;
			cy += v.y;
			cz += v.z;
		}
		float n = static_cast<float>(vertices.size());
		float centX = static_cast<float>(cx / n);
		float centY = static_cast<float>(cy / n);
		float centZ = static_cast<float>(cz / n);

		// Translate to origin and compute extents in one pass
		Vec3 lo{1e30f, 1e30f, 1e30f};
		Vec3 hi{-1e30f, -1e30f, -1e30f};
		for (auto &v : vertices)
		{
			v.x -= centX;
			v.y -= centY;
			v.z -= centZ;
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

	// Build deduplicated edge list from faces using hash set (O(E) instead of O(E log E))
	void buildEdges()
	{
		edges.clear();

		auto edgeKey = [](int a, int b) -> int64_t {
			if (a > b) std::swap(a, b);
			return (static_cast<int64_t>(a) << 32) | static_cast<int64_t>(static_cast<uint32_t>(b));
		};

		std::unordered_set<int64_t> seen;
		seen.reserve(faces.size() * 3);
		edges.reserve(faces.size() * 3 / 2);

		for (const auto &f : faces)
		{
			int nv = static_cast<int>(f.indices.size());
			for (int i = 0; i < nv; ++i)
			{
				int a = f.indices[i];
				int b = f.indices[(i + 1) % nv];
				int64_t key = edgeKey(a, b);
				if (seen.insert(key).second)
				{
					if (a > b) std::swap(a, b);
					edges.push_back(Edge{a, b});
				}
			}
		}
	}

	// Load from .obj file using memory-mapped I/O and fast parsing.
	static bool load(const std::string &path, ObjMesh &out, std::string *err = nullptr)
	{
		out.vertices.clear();
		out.faces.clear();
		out.edges.clear();

		// Memory-map the file
		const char *data = nullptr;
		size_t fileSize = 0;

#ifdef _WIN32
		HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			if (err) *err = "Cannot open file: " + path;
			return false;
		}
		LARGE_INTEGER liSize;
		GetFileSizeEx(hFile, &liSize);
		fileSize = static_cast<size_t>(liSize.QuadPart);
		HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!hMap) { CloseHandle(hFile); if (err) *err = "Cannot map file"; return false; }
		data = static_cast<const char *>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
		if (!data) { CloseHandle(hMap); CloseHandle(hFile); if (err) *err = "Cannot map view"; return false; }
#else
		int fd = open(path.c_str(), O_RDONLY);
		if (fd < 0)
		{
			if (err) *err = "Cannot open file: " + path;
			return false;
		}
		struct stat st;
		if (fstat(fd, &st) != 0) { close(fd); if (err) *err = "Cannot stat file"; return false; }
		fileSize = static_cast<size_t>(st.st_size);
		if (fileSize == 0) { close(fd); if (err) *err = "Empty file"; return false; }
		data = static_cast<const char *>(mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0));
		if (data == MAP_FAILED) { close(fd); if (err) *err = "Cannot mmap file"; return false; }
		// Hint sequential access for kernel readahead
		madvise(const_cast<char *>(data), fileSize, MADV_SEQUENTIAL);
#endif

		// Parse the memory-mapped buffer
		const char *p = data;
		const char *end = data + fileSize;

		// Pre-scan to estimate vertex/face counts for reservation
		size_t estVerts = 0, estFaces = 0;
		{
			const char *scan = p;
			while (scan < end)
			{
				if (*scan == 'v' && (scan == data || *(scan-1) == '\n' || *(scan-1) == '\r'))
				{
					if (scan + 1 < end && (scan[1] == ' ' || scan[1] == '\t'))
						++estVerts;
				}
				else if (*scan == 'f' && (scan == data || *(scan-1) == '\n' || *(scan-1) == '\r'))
				{
					if (scan + 1 < end && (scan[1] == ' ' || scan[1] == '\t'))
						++estFaces;
				}
				// Skip to next line
				while (scan < end && *scan != '\n') ++scan;
				if (scan < end) ++scan;
			}
		}
		out.vertices.reserve(estVerts);
		out.faces.reserve(estFaces);

		while (p < end)
		{
			// Skip whitespace at start of line
			while (p < end && (*p == ' ' || *p == '\t')) ++p;

			if (p >= end) break;

			if (*p == '#' || *p == '\n' || *p == '\r')
			{
				// Comment or blank line — skip to next line
				while (p < end && *p != '\n') ++p;
				if (p < end) ++p;
				continue;
			}

			if (*p == 'v' && p + 1 < end && (p[1] == ' ' || p[1] == '\t'))
			{
				// Vertex line: "v x y z"
				p += 2;
				Vec3 v;
				v.x = fastParseFloat(p, end);
				v.y = fastParseFloat(p, end);
				v.z = fastParseFloat(p, end);
				out.vertices.push_back(v);
			}
			else if (*p == 'f' && p + 1 < end && (p[1] == ' ' || p[1] == '\t'))
			{
				// Face line: "f v1 v2 v3 ..." or "f v1/vt1 v2/vt2 ..." etc.
				p += 2;
				Face face;
				int numVerts = static_cast<int>(out.vertices.size());
				while (p < end && *p != '\n' && *p != '\r')
				{
					// Skip spaces
					while (p < end && (*p == ' ' || *p == '\t')) ++p;
					if (p >= end || *p == '\n' || *p == '\r') break;

					// Parse integer (vertex index)
					int vi = fastParseInt(p, end);
					if (vi != 0)
					{
						if (vi < 0)
							vi = numVerts + vi;
						else
							vi -= 1;
						face.indices.push_back(vi);
					}

					// Skip past any /vt/vn components
					while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
						++p;
				}
				if (face.indices.size() >= 3)
					out.faces.push_back(std::move(face));
			}

			// Skip to next line
			while (p < end && *p != '\n') ++p;
			if (p < end) ++p;
		}

		// Unmap
#ifdef _WIN32
		UnmapViewOfFile(data);
		CloseHandle(hMap);
		CloseHandle(hFile);
#else
		munmap(const_cast<char *>(data), fileSize);
		close(fd);
#endif

		if (out.vertices.empty())
		{
			if (err) *err = "No vertices found in OBJ file";
			return false;
		}

		out.normalize();
		out.buildEdges();
		return true;
	}

private:
	// Fast float parser: skips leading whitespace, parses sign + digits + decimal
	static float fastParseFloat(const char *&p, const char *end)
	{
		// Skip whitespace
		while (p < end && (*p == ' ' || *p == '\t')) ++p;
		if (p >= end) return 0.0f;

		char *endPtr = nullptr;
		float val = std::strtof(p, &endPtr);
		if (endPtr > p)
			p = endPtr;
		return val;
	}

	// Fast integer parser
	static int fastParseInt(const char *&p, const char *end)
	{
		while (p < end && (*p == ' ' || *p == '\t')) ++p;
		if (p >= end) return 0;

		bool neg = false;
		if (*p == '-') { neg = true; ++p; }
		else if (*p == '+') { ++p; }

		int val = 0;
		bool hasDigit = false;
		while (p < end && *p >= '0' && *p <= '9')
		{
			val = val * 10 + (*p - '0');
			++p;
			hasDigit = true;
		}
		if (!hasDigit) return 0;
		return neg ? -val : val;
	}
};
