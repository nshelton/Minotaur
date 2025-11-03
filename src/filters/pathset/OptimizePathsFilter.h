#pragma once

#include <atomic>
#include "../Filter.h"

struct OptimizePathsFilter : public FilterTyped<PathSet, PathSet>
{
	OptimizePathsFilter()
	{
		m_parameters["mergeEnabled"] = FilterParameter{
			"Enable Merge (0/1)",
			0.0f,
			1.0f,
			0.0f
		};
		m_parameters["mergeDistanceMm"] = FilterParameter{
			"Merge Distance (mm)",
			0.0f,
			10.0f,
			0.0f
		};
		m_parameters["reorder"] = FilterParameter{
			"Reorder (0/1)",
			0.0f,
			1.0f,
			1.0f
		};
	}

	const char *name() const override { return "Optimize Paths"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const PathSet &in, PathSet &out) const override;
};


