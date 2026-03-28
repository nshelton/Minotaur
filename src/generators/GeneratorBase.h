#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <atomic>

#include <glog/logging.h>

#include "filters/Filter.h"   // reuse FilterParameter
#include "filters/Types.h"

// Base interface for all generators.
// A generator produces a layer from parameters alone — it has no input.
// This mirrors FilterBase but uses generate() instead of apply().
struct GeneratorBase
{
	GeneratorBase() = default;
	virtual ~GeneratorBase() = default;

	// Descriptive name used in UI and serialization
	virtual const char *name() const = 0;

	// Output layer type
	virtual LayerKind outputKind() const = 0;

	// Monotonic version counter that increments whenever a parameter changes.
	// Used by Entity::tickGenerator() to detect when to re-run the generator.
	virtual uint64_t paramVersion() const = 0;

	// Produce output. Implementations must write a valid layer of outputKind() into out.
	virtual void generate(LayerPtr &out) const = 0;

	// Deep-copy this generator, including all parameters.
	// Subclasses with extra state (e.g. string content) must override this.
	virtual std::unique_ptr<GeneratorBase> clone() const = 0;

	// Parameter map — same type as FilterBase for consistent UI rendering
	std::map<std::string, FilterParameter> m_parameters;

	void setParameter(const std::string &key, float value)
	{
		auto it = m_parameters.find(key);
		if (it != m_parameters.end())
		{
			it->second.value = value;
			m_version.fetch_add(1);
		}
		else
		{
			LOG(INFO) << "tried to set \"" << key << "\", not found on generator " << name();
		}
	}

protected:
	std::atomic<uint64_t> m_version{1};
};
