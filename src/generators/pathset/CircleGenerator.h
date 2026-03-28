#pragma once
#include <cmath>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"

// Generates a circle centred at the origin in local entity space.
struct CircleGenerator : public GeneratorTyped<CircleGenerator, PathSet>
{
	CircleGenerator()
	{
		m_parameters["radius_mm"] = FilterParameter{"Radius (mm)",  1.0f, 200.0f, 50.0f};
		m_parameters["segments"]  = FilterParameter{"Segments",     3.0f, 256.0f, 96.0f, FilterParameter::Int};
	}

	const char *name() const override { return "Circle"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(PathSet &out) const override
	{
		float radius   = m_parameters.at("radius_mm").value;
		int   segments = static_cast<int>(std::lround(m_parameters.at("segments").value));
		if (segments < 3) segments = 3;

		out.paths.clear();
		Path path;
		path.closed = true;
		const float twoPi = 6.28318530718f;
		for (int i = 0; i < segments; ++i)
		{
			float ang = (static_cast<float>(i) / static_cast<float>(segments)) * twoPi;
			path.points.push_back(Vec2{radius * cosf(ang), radius * sinf(ang)});
		}
		out.paths.push_back(std::move(path));
	}
};
