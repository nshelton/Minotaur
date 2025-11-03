#pragma once

#include "../Filter.h"

struct LaplacianSmoothFilter : public FilterTyped<PathSet, PathSet>
{
	LaplacianSmoothFilter()
	{
		m_parameters["iterations"] = FilterParameter{
			"Iterations",
			0.0f,
			50.0f,
			5.0f
		};
		m_parameters["weight"] = FilterParameter{
			"Weight",
			0.0f,
			1.0f,
			0.5f
		};
	}

	const char *name() const override { return "Laplacian Smooth"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const PathSet &in, PathSet &out) const override;
};



