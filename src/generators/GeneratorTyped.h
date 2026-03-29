#pragma once

#include <type_traits>
#include <memory>

#include "generators/GeneratorBase.h"
#include "filters/Filter.h"   // for ensure<T> and as<T>

// Strongly-typed convenience shim for implementing generators.
// Derived is the concrete subclass (CRTP); OutT is the concrete output type.
//
// Usage:
//   struct CircleGenerator : public GeneratorTyped<CircleGenerator, PathSet> { ... };
template <typename Derived, typename OutT>
struct GeneratorTyped : public GeneratorBase
{
	LayerKind outputKind() const override
	{
		if constexpr (std::is_same_v<OutT, Bitmap>)       return LayerKind::Bitmap;
		else if constexpr (std::is_same_v<OutT, FloatImage>)  return LayerKind::FloatImage;
		else if constexpr (std::is_same_v<OutT, ColorImage>)  return LayerKind::ColorImage;
		else                                                    return LayerKind::PathSet;
	}

	void generate(LayerPtr &out) const override
	{
		ensure<OutT>(out);
		OutT &dst = as<OutT>(out);
		generateTyped(dst);
	}

	// Default clone: copies float parameters and string parameters.
	// Subclasses with additional state must override clone().
	std::unique_ptr<GeneratorBase> clone() const override
	{
		auto copy = std::make_unique<Derived>();
		for (const auto &kv : m_parameters)
			copy->setParameter(kv.first, kv.second.value);
		for (const auto &kv : m_stringParameters)
			copy->setStringParameter(kv.first, kv.second);
		return copy;
	}

	// Implemented by concrete generators
	virtual void generateTyped(OutT &out) const = 0;
};
