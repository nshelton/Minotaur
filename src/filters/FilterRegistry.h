#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "filters/Types.h"
#include "filters/Filter.h"

struct FilterInfo
{
	std::string name;
	LayerKind inputKind;
	LayerKind outputKind;
	std::function<std::unique_ptr<FilterBase>()> factory;
};

class FilterRegistry
{
public:
	static FilterRegistry &instance();

	// Register a filter type with metadata and a factory
	void registerFilter(const FilterInfo &info);

	// Return all registered filters
	const std::vector<FilterInfo> &all() const;

	// Get filters that accept a given input kind
	std::vector<FilterInfo> byInput(LayerKind kind) const;

	// One-time initialization of built-in filters
	static void initDefaults();

	// Helper to build a button label like: "Blur (Bitmap to Bitmap)"
	static std::string makeButtonLabel(const FilterInfo &info);

private:
	std::vector<FilterInfo> m_filters;
};


