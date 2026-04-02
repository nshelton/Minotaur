#pragma once

#include <cmath>
#include <string>
#include <vector>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"
#include <glog/logging.h>

// Forward declare nanosvg types to avoid pulling the header here.
// The implementation needs NANOSVG_IMPLEMENTATION defined in exactly one TU.
struct NSVGimage;

// Parsed SVG data stored independently of nanosvg so we can free the NSVGimage
// right after parsing and keep only what we need.
struct SvgPathData
{
	struct CubicBezier { float x0,y0, x1,y1, x2,y2, x3,y3; };
	struct SubPath
	{
		std::vector<CubicBezier> beziers;
		bool closed{false};
	};
	std::vector<SubPath> subPaths;
	float width{0};
	float height{0};

	bool empty() const { return subPaths.empty(); }
};

// Generates a PathSet from an SVG file.
//
// The SVG is parsed on load via nanosvg (requesting mm units).  All shapes/paths
// are stored as cubic bezier segments in SvgPathData.  generateTyped() flattens
// the beziers into polylines with a configurable tolerance (chord-error in mm).
//
// Parameters:
//   tolerance  - bezier flattening chord error (mm).  Lower = more points.
//   scale      - uniform scale factor applied to all points
//
// String parameters:
//   file       - path to the .svg file

struct SvgGenerator : public GeneratorTyped<SvgGenerator, PathSet>
{
	SvgGenerator()
	{
		m_parameters["tolerance"] = FilterParameter{"Tolerance (mm)", 0.05f, 10.0f, 0.5f};
		m_parameters["scale"]     = FilterParameter{"Scale", 0.01f, 20.0f, 1.0f};
		m_stringParameters["file"] = "";
	}

	explicit SvgGenerator(const std::string &svgPath)
		: SvgGenerator()
	{
		m_stringParameters["file"] = svgPath;
		loadSvg(svgPath);
	}

	const char *name() const override { return "SVG"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	bool hasSvg() const { return !m_svg.empty(); }

	void generateTyped(PathSet &out) const override
	{
		out.paths.clear();
		if (m_svg.empty()) return;

		float tol = m_parameters.at("tolerance").value;
		float scale = m_parameters.at("scale").value;

		// SVG uses Y-down (origin at top-left); the page uses Y-up.
		// Flip Y around the SVG document height so the image appears right-side-up.
		float h = m_svg.height;

		for (const auto &sp : m_svg.subPaths)
		{
			Path path;
			path.closed = sp.closed;

			for (size_t i = 0; i < sp.beziers.size(); ++i)
			{
				const auto &b = sp.beziers[i];
				// Flip Y: y' = (height - y) * scale
				float x0 = b.x0 * scale, y0 = (h - b.y0) * scale;
				float x1 = b.x1 * scale, y1 = (h - b.y1) * scale;
				float x2 = b.x2 * scale, y2 = (h - b.y2) * scale;
				float x3 = b.x3 * scale, y3 = (h - b.y3) * scale;

				// Add start point only for first bezier (subsequent ones
				// continue from previous endpoint)
				if (i == 0)
					path.points.push_back(Vec2(x0, y0));

				flattenCubic(path.points,
					x0, y0, x1, y1, x2, y2, x3, y3,
					tol, 0);
			}

			if (path.points.size() >= 2)
				out.paths.push_back(std::move(path));
		}
	}

	std::unique_ptr<GeneratorBase> clone() const override
	{
		auto copy = std::make_unique<SvgGenerator>();
		for (const auto &kv : m_parameters)
			copy->setParameter(kv.first, kv.second.value);
		for (const auto &kv : m_stringParameters)
			copy->setStringParameter(kv.first, kv.second);
		if (!m_svg.empty())
			copy->m_svg = m_svg;
		return copy;
	}

	bool loadSvg(const std::string &path);

	void onStringParameterChanged(const std::string &key) override
	{
		if (key == "file")
			loadSvg(m_stringParameters["file"]);
	}

private:
	SvgPathData m_svg;

	// Recursive adaptive subdivision of a cubic bezier.
	// Adds points to `pts` when a segment is flat enough (chord error < tol).
	static void flattenCubic(std::vector<Vec2> &pts,
		float x0, float y0, float x1, float y1,
		float x2, float y2, float x3, float y3,
		float tol, int depth)
	{
		constexpr int MAX_DEPTH = 12;
		if (depth > MAX_DEPTH)
		{
			pts.push_back(Vec2(x3, y3));
			return;
		}

		// Midpoint chord-error test: distance from control points to the
		// line segment (p0→p3). If both are within tolerance, emit endpoint.
		float dx = x3 - x0, dy = y3 - y0;
		float d2 = dx * dx + dy * dy;

		if (d2 > 1e-12f)
		{
			float d1 = std::fabs((x1 - x3) * dy - (y1 - y3) * dx);
			float d2v = std::fabs((x2 - x3) * dy - (y2 - y3) * dx);
			float err = (d1 + d2v) * (d1 + d2v) / d2;
			if (err <= tol * tol)
			{
				pts.push_back(Vec2(x3, y3));
				return;
			}
		}
		else
		{
			// Degenerate — control points coincide
			pts.push_back(Vec2(x3, y3));
			return;
		}

		// De Casteljau split at t=0.5
		float x01 = (x0+x1)*0.5f, y01 = (y0+y1)*0.5f;
		float x12 = (x1+x2)*0.5f, y12 = (y1+y2)*0.5f;
		float x23 = (x2+x3)*0.5f, y23 = (y2+y3)*0.5f;
		float x012 = (x01+x12)*0.5f, y012 = (y01+y12)*0.5f;
		float x123 = (x12+x23)*0.5f, y123 = (y12+y23)*0.5f;
		float x0123 = (x012+x123)*0.5f, y0123 = (y012+y123)*0.5f;

		flattenCubic(pts, x0,y0, x01,y01, x012,y012, x0123,y0123, tol, depth+1);
		flattenCubic(pts, x0123,y0123, x123,y123, x23,y23, x3,y3, tol, depth+1);
	}
};
