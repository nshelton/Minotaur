#pragma once
#include "generators/GeneratorTyped.h"
#include "core/Core.h"

// Generates a grid of lines (horizontal, vertical, or both) centred at the origin.
struct GridGenerator : public GeneratorTyped<GridGenerator, PathSet>
{
	GridGenerator()
	{
		m_parameters["width_mm"]    = FilterParameter{"Width (mm)",    1.0f, 500.0f, 100.0f};
		m_parameters["height_mm"]   = FilterParameter{"Height (mm)",   1.0f, 500.0f, 100.0f};
		m_parameters["num_lines"]   = FilterParameter{"Num Lines",     1.0f, 200.0f, 10.0f, FilterParameter::Int};
		m_parameters["direction"]   = FilterParameter{"Direction",     0.0f,   2.0f,  0.0f, FilterParameter::Enum};
		m_parameters["direction"].enumLabels = {"Horizontal", "Vertical", "Both"};
	}

	const char *name() const override { return "Grid"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(PathSet &out) const override
	{
		float w        = m_parameters.at("width_mm").value;
		float h        = m_parameters.at("height_mm").value;
		int   n        = static_cast<int>(std::lround(m_parameters.at("num_lines").value));
		int   dir      = static_cast<int>(std::lround(m_parameters.at("direction").value));
		if (n < 1) n = 1;

		float halfW = w * 0.5f;
		float halfH = h * 0.5f;

		out.paths.clear();

		// Horizontal lines (constant Y, spanning X)
		if (dir == 0 || dir == 2)
		{
			for (int i = 0; i < n; ++i)
			{
				float t = (n == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
				float y = -halfH + t * h;
				Path p;
				p.closed = false;
				p.points.push_back(Vec2{-halfW, y});
				p.points.push_back(Vec2{ halfW, y});
				out.paths.push_back(std::move(p));
			}
		}

		// Vertical lines (constant X, spanning Y)
		if (dir == 1 || dir == 2)
		{
			for (int i = 0; i < n; ++i)
			{
				float t = (n == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
				float x = -halfW + t * w;
				Path p;
				p.closed = false;
				p.points.push_back(Vec2{x, -halfH});
				p.points.push_back(Vec2{x,  halfH});
				out.paths.push_back(std::move(p));
			}
		}
	}
};
