#include "FilterRegistry.h"

#include <algorithm>
#include <fmt/format.h>

#include "filters/bitmap/BlurFilter.h"
#include "filters/bitmap/ThresholdFilter.h"
#include "filters/bitmap/TraceFilter.h"
#include "filters/bitmap/TraceBlobsFilter.h"
#include "filters/pathset/SimplifyFilter.h"

namespace {
	inline const char *toString(LayerKind k)
	{
		return (k == LayerKind::Bitmap) ? "Bitmap" : "PathSet";
	}
}

FilterRegistry &FilterRegistry::instance()
{
	static FilterRegistry g_instance;
	return g_instance;
}

void FilterRegistry::registerFilter(const FilterInfo &info)
{
	// Avoid duplicates by name + io kinds
	auto it = std::find_if(m_filters.begin(), m_filters.end(), [&](const FilterInfo &f) {
		return f.name == info.name && f.inputKind == info.inputKind && f.outputKind == info.outputKind;
	});
	if (it == m_filters.end())
	{
		m_filters.push_back(info);
	}
}

const std::vector<FilterInfo> &FilterRegistry::all() const
{
	return m_filters;
}

std::vector<FilterInfo> FilterRegistry::byInput(LayerKind kind) const
{
	std::vector<FilterInfo> out;
	out.reserve(m_filters.size());
	for (const auto &fi : m_filters)
	{
		if (fi.inputKind == kind)
			out.push_back(fi);
	}
	return out;
}

std::string FilterRegistry::makeButtonLabel(const FilterInfo &info)
{
	return fmt::format("{} ({} to {})", info.name, toString(info.inputKind), toString(info.outputKind));
}

void FilterRegistry::initDefaults()
{
	static bool initialized = false;
	if (initialized) return;
	initialized = true;

	auto &reg = instance();

	reg.registerFilter(FilterInfo{
		"Blur",
		LayerKind::Bitmap,
		LayerKind::Bitmap,
		[]() { return std::make_unique<BlurFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Trace",
		LayerKind::Bitmap,
		LayerKind::PathSet,
		[]() { return std::make_unique<TraceFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Blobs",
		LayerKind::Bitmap,
		LayerKind::PathSet,
		[]() { return std::make_unique<TraceBlobsFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Threshold",
		LayerKind::Bitmap,
		LayerKind::Bitmap,
		[]() { return std::make_unique<ThresholdFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Simplify",
		LayerKind::PathSet,
		LayerKind::PathSet,
		[]() { return std::make_unique<SimplifyFilter>(); }
	});
}


