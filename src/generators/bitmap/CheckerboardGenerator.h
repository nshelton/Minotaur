#pragma once
#include <cmath>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"

// Generates a checkerboard bitmap.
struct CheckerboardGenerator : public GeneratorTyped<CheckerboardGenerator, Bitmap>
{
	CheckerboardGenerator()
	{
		m_parameters["width_px"]       = FilterParameter{"Width (px)",       16.0f, 2048.0f, 256.0f, FilterParameter::Int};
		m_parameters["height_px"]      = FilterParameter{"Height (px)",      16.0f, 2048.0f, 256.0f, FilterParameter::Int};
		m_parameters["block_px"]       = FilterParameter{"Block Size (px)",   1.0f,  256.0f,  16.0f, FilterParameter::Int};
		m_parameters["pixel_size_mm"]  = FilterParameter{"Pixel Size (mm)",  0.01f,    5.0f,   0.5f};
	}

	const char *name() const override { return "Checkerboard"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(Bitmap &out) const override
	{
		int   w     = static_cast<int>(std::lround(m_parameters.at("width_px").value));
		int   h     = static_cast<int>(std::lround(m_parameters.at("height_px").value));
		int   block = static_cast<int>(std::lround(m_parameters.at("block_px").value));
		float psz   = m_parameters.at("pixel_size_mm").value;
		if (block < 1) block = 1;

		out.width_px      = w;
		out.height_px     = h;
		out.pixel_size_mm = psz;
		out.pixels.resize(static_cast<size_t>(w * h));

		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				bool on = (((x / block) ^ (y / block)) & 1) == 0;
				out.pixels[static_cast<size_t>(y * w + x)] = on ? 200 : 40;
			}
		}
	}
};
