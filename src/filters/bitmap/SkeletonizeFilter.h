#pragma once

#include <atomic>

#include "../Filter.h"

// Skeletonize black pixels and trace the 1px-wide skeleton into polylines
struct SkeletonizeFilter : public FilterTyped<Bitmap, PathSet>
{
	SkeletonizeFilter()
	{
		m_parameters["threshold"] = FilterParameter{
			"Black Threshold",
			0.0f,
			255.0f,
			128.0f
		};
		m_parameters["pruneIters"] = FilterParameter{
			"Prune Iterations",
			0.0f,
			50.0f,
			5.0f
		};
		m_parameters["tolerancePx"] = FilterParameter{
			"Simplify (px)",
			0.0f,
			10.0f,
			1.0f
		};
		m_parameters["downsample"] = FilterParameter{
			"Downsample",
			1.0f,
			8.0f,
			1.0f
		};
	}

	const char *name() const override { return "Skeletonize"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const Bitmap &in, PathSet &out) const override;
};


