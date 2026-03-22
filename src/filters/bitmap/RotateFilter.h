#pragma once

#include "../Filter.h"

// Rotates a bitmap by an arbitrary angle. The output bitmap is resized to
// fit the rotated bounding box, with new corner pixels filled white (255).
// Uses bilinear interpolation for smooth sampling.
struct RotateFilter : public FilterTyped<Bitmap, Bitmap>
{
	RotateFilter()
	{
		m_parameters["angle"] = FilterParameter{
			"Angle (deg)", -180.0f, 180.0f, 0.0f};
	}

	const char *name() const override { return "Rotate"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const Bitmap &in, Bitmap &out) const override;
};
