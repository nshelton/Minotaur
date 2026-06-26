#pragma once

#include "../Filter.h"

// Fast Marching Method contour filter.
// A wavefront expands from a seed point across the image. Pixel brightness
// controls the local wave speed (bright = fast, rings spread apart; dark =
// slow, rings compress). Solving the Eikonal equation yields a travel-time
// map; slicing it at evenly-spaced levels gives topographic iso-lines.
struct FastMarchingFilter : public FilterTyped<Bitmap, PathSet>
{
	FastMarchingFilter()
	{
		m_parameters["seedX"] = FilterParameter{"seed x", 0.0f, 1.0f, 0.5f};
		m_parameters["seedY"] = FilterParameter{"seed y", 0.0f, 1.0f, 0.5f};
		m_parameters["minSpeed"] = FilterParameter{"min speed", 0.01f, 1.0f, 0.05f};
		m_parameters["maxSpeed"] = FilterParameter{"max speed", 1.0f, 64.0f, 1.0f};
		m_parameters["contrast"] = FilterParameter{"contrast", 0.1f, 5.0f, 1.0f};
		m_parameters["invert"] = FilterParameter{"invert", 0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["levelSpacing"] = FilterParameter{"level spacing", 0.005f, 0.5f, 0.05f};
	}

	const char *name() const override { return "Fast Marching"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const Bitmap &in, PathSet &out) const override;
};
