#include "FilterRegistry.h"

#include <algorithm>
#include <fmt/format.h>

#include "filters/bitmap/BlurFilter.h"
#include "filters/bitmap/ThresholdFilter.h"
#include "filters/bitmap/LevelsFilter.h"
#include "filters/bitmap/TraceFilter.h"
#include "filters/bitmap/TraceBlobsFilter.h"
#include "filters/bitmap/CannyFilter.h"
#include "filters/bitmap/LineHatchFilter.h"
#include "filters/bitmap/SkeletonizeFilter.h"
#include "filters/bitmap/ClaheFilter.h"
#include "filters/bitmap/BitmapToFloatFilter.h"
#include "filters/pathset/SimplifyFilter.h"
#include "filters/pathset/SmoothFilter.h"
#include "filters/pathset/OptimizePathsFilter.h"
#include "filters/pathset/LaplacianSmoothFilter.h"
#include "filters/pathset/CurlNoiseFilter.h"

namespace {
	inline const char *toString(LayerKind k)
	{
        switch (k) {
        case LayerKind::Bitmap: return "Bitmap";
        case LayerKind::PathSet: return "PathSet";
        case LayerKind::FloatImage: return "Float";
        }
        return "?";
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
		"Levels",
		LayerKind::Bitmap,
		LayerKind::Bitmap,
		[]() { return std::make_unique<LevelsFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"CLAHE",
		LayerKind::Bitmap,
		LayerKind::Bitmap,
		[]() { return std::make_unique<ClaheFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Trace",
		LayerKind::Bitmap,
		LayerKind::PathSet,
		[]() { return std::make_unique<TraceFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Line Hatch",
		LayerKind::Bitmap,
		LayerKind::PathSet,
		[]() { return std::make_unique<LineHatchFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Blobs",
		LayerKind::Bitmap,
		LayerKind::PathSet,
		[]() { return std::make_unique<TraceBlobsFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Skeletonize",
		LayerKind::Bitmap,
		LayerKind::PathSet,
		[]() { return std::make_unique<SkeletonizeFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Threshold",
		LayerKind::Bitmap,
		LayerKind::Bitmap,
		[]() { return std::make_unique<ThresholdFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Canny",
		LayerKind::Bitmap,
		LayerKind::Bitmap,
		[]() { return std::make_unique<CannyFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Simplify",
		LayerKind::PathSet,
		LayerKind::PathSet,
		[]() { return std::make_unique<SimplifyFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Smooth",
		LayerKind::PathSet,
		LayerKind::PathSet,
		[]() { return std::make_unique<SmoothFilter>(); }
	});

	reg.registerFilter(FilterInfo{
		"Laplacian Smooth",
		LayerKind::PathSet,
		LayerKind::PathSet,
		[]() { return std::make_unique<LaplacianSmoothFilter>(); }
	});

    reg.registerFilter(FilterInfo{
        "Curl Noise Displace",
        LayerKind::PathSet,
        LayerKind::PathSet,
        []() { return std::make_unique<CurlNoiseFilter>(); }
    });

	reg.registerFilter(FilterInfo{
		"Optimize Paths",
		LayerKind::PathSet,
		LayerKind::PathSet,
		[]() { return std::make_unique<OptimizePathsFilter>(); }
	});

    reg.registerFilter(FilterInfo{
        "Bitmap to Float",
        LayerKind::Bitmap,
        LayerKind::FloatImage,
        []() { return std::make_unique<BitmapToFloatFilter>(); }
    });
}


