#pragma once
#include "generators/GeneratorTyped.h"
#include "core/Core.h"

// Generates an axis-aligned square centred at the origin in local entity space.
struct SquareGenerator : public GeneratorTyped<SquareGenerator, PathSet>
{
	SquareGenerator()
	{
		m_parameters["side_mm"] = FilterParameter{"Side (mm)", 1.0f, 400.0f, 80.0f};
	}

	const char *name() const override { return "Square"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(PathSet &out) const override
	{
		float h = m_parameters.at("side_mm").value * 0.5f;

		out.paths.clear();
		Path path;
		path.closed = true;
		path.points.push_back(Vec2{-h, -h});
		path.points.push_back(Vec2{ h, -h});
		path.points.push_back(Vec2{ h,  h});
		path.points.push_back(Vec2{-h,  h});
		out.paths.push_back(std::move(path));
	}
};
