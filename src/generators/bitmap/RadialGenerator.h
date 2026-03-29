#pragma once
#include <cmath>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"

// Generates a radial gradient bitmap (white centre, black edges).
struct RadialGenerator : public GeneratorTyped<RadialGenerator, Bitmap>
{
	RadialGenerator()
	{
		m_parameters["width_px"]       = FilterParameter{"Width (px)",       16.0f, 2048.0f, 256.0f, FilterParameter::Int};
		m_parameters["height_px"]      = FilterParameter{"Height (px)",      16.0f, 2048.0f, 256.0f, FilterParameter::Int};
		m_parameters["pixel_size_mm"]  = FilterParameter{"Pixel Size (mm)",  0.01f,    5.0f,   0.5f};
	}

	const char *name() const override { return "Radial"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(Bitmap &out) const override
	{
		int   w   = static_cast<int>(std::lround(m_parameters.at("width_px").value));
		int   h   = static_cast<int>(std::lround(m_parameters.at("height_px").value));
		float psz = m_parameters.at("pixel_size_mm").value;

		out.width_px      = w;
		out.height_px     = h;
		out.pixel_size_mm = psz;
		out.pixels.resize(static_cast<size_t>(w * h));

		float cx   = (w - 1) * 0.5f;
		float cy   = (h - 1) * 0.5f;
		float maxR = std::sqrt(cx * cx + cy * cy);
		if (maxR < 1.0f) maxR = 1.0f;

		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				float dx = static_cast<float>(x) - cx;
				float dy = static_cast<float>(y) - cy;
				float r  = std::sqrt(dx * dx + dy * dy);
				float t  = r / maxR;
				out.pixels[static_cast<size_t>(y * w + x)] =
					static_cast<uint8_t>(std::round((1.0f - t) * 255.0f));
			}
		}
	}
};
