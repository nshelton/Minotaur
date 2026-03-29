#pragma once
#include <cmath>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"

// Generates a star centred at the origin in local entity space.
struct StarGenerator : public GeneratorTyped<StarGenerator, PathSet>
{
	StarGenerator()
	{
		m_parameters["outer_radius_mm"] = FilterParameter{"Outer Radius (mm)", 1.0f, 200.0f, 60.0f};
		m_parameters["inner_radius_mm"] = FilterParameter{"Inner Radius (mm)", 1.0f, 200.0f, 30.0f};
		m_parameters["points"]          = FilterParameter{"Points",             2.0f,  20.0f,  5.0f, FilterParameter::Int};
	}

	const char *name() const override { return "Star"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(PathSet &out) const override
	{
		float outer  = m_parameters.at("outer_radius_mm").value;
		float inner  = m_parameters.at("inner_radius_mm").value;
		int   points = static_cast<int>(std::lround(m_parameters.at("points").value));
		if (points < 2) points = 2;

		out.paths.clear();
		Path path;
		path.closed = true;
		const float pi = 3.14159265359f;
		int verts = points * 2;
		for (int i = 0; i < verts; ++i)
		{
			float t   = static_cast<float>(i) / static_cast<float>(verts);
			float ang = t * 2.0f * pi - pi * 0.5f; // start at top
			float rad = (i % 2 == 0) ? outer : inner;
			path.points.push_back(Vec2{rad * cosf(ang), rad * sinf(ang)});
		}
		out.paths.push_back(std::move(path));
	}
};
