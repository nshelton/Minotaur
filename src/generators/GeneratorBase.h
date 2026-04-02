#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <atomic>
#include <memory>

#include <glog/logging.h>

#include "filters/Filter.h"   // reuse FilterParameter
#include "filters/Types.h"

// Base interface for all generators.
// A generator produces a layer from parameters alone — it has no input.
// This mirrors FilterBase but uses generate() instead of apply().
//
// Two execution models are supported:
//
//   Synchronous (default, isAsync() == false):
//     generate(out) is called directly on the render thread whenever params
//     change. Suitable for cheap generators (shapes, procedural bitmaps).
//
//   Asynchronous (isAsync() == true):
//     Used for generators that do expensive work (network fetch, file I/O,
//     heavy computation). The flow is:
//       1. tickGenerator() calls startGenerate() when params are stale.
//       2. Each frame, isReady() is polled (non-blocking).
//       3. When isReady() returns true, collectResult(out) is called to
//          retrieve the completed layer on the render thread.
//     The generator is responsible for running its own background thread.
struct GeneratorBase
{
	GeneratorBase() = default;
	virtual ~GeneratorBase() = default;

	// Descriptive name used in UI and serialization
	virtual const char *name() const = 0;

	// Output layer type
	virtual LayerKind outputKind() const = 0;

	// Monotonic version counter that increments whenever any parameter changes.
	// Used by Entity::tickGenerator() to detect when to re-run the generator.
	virtual uint64_t paramVersion() const = 0;

	// -------------------------------------------------------------------------
	// Synchronous generation (override for simple generators)
	// -------------------------------------------------------------------------

	// Produce output synchronously. Called on the render thread.
	// Only called when isAsync() == false.
	virtual void generate(LayerPtr &out) const {}

	// -------------------------------------------------------------------------
	// Asynchronous generation (override for expensive generators)
	// -------------------------------------------------------------------------

	// Return true if this generator uses the async model.
	virtual bool isAsync() const { return false; }

	// Kick off background work. Called when paramVersion() has advanced.
	// Implementations should start a thread (or reuse a thread pool) and
	// return immediately. Must be safe to call again before a previous job
	// finishes (i.e. cancel/restart semantics).
	virtual void startGenerate() {}

	// Non-blocking poll. Returns true when the last startGenerate() job is
	// done and its result is ready to be collected.
	virtual bool isReady() const { return true; }

	// Transfer the completed result into `out`. Called on the render thread
	// only after isReady() has returned true. Resets the ready flag so that
	// the next startGenerate() starts fresh.
	virtual void collectResult(LayerPtr &out) {}

	// -------------------------------------------------------------------------
	// Deep copy
	// -------------------------------------------------------------------------

	// Deep-copy this generator, including all parameters and string parameters.
	// Subclasses with extra state must override this.
	virtual std::unique_ptr<GeneratorBase> clone() const = 0;

	// -------------------------------------------------------------------------
	// Float parameters  (sliders / checkboxes / combos in UI)
	// -------------------------------------------------------------------------

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

	// -------------------------------------------------------------------------
	// String parameters  (text inputs, file paths, place names, etc.)
	// -------------------------------------------------------------------------

	std::map<std::string, std::string> m_stringParameters;

	void setStringParameter(const std::string &key, const std::string &value)
	{
		auto it = m_stringParameters.find(key);
		if (it != m_stringParameters.end())
		{
			it->second = value;
			m_version.fetch_add(1);
			onStringParameterChanged(key);
		}
		else
		{
			LOG(INFO) << "tried to set string param \"" << key << "\", not found on generator " << name();
		}
	}

	// Override to react when a string parameter is set (e.g. reload a file).
	// Called from setStringParameter after the value is stored.
	virtual void onStringParameterChanged(const std::string &) {}

	const std::string &stringParameter(const std::string &key) const
	{
		static const std::string empty;
		auto it = m_stringParameters.find(key);
		return (it != m_stringParameters.end()) ? it->second : empty;
	}

	// Progress reporting for long-running generators (0.0 .. 1.0)
	float progress() const { return m_progress.load(std::memory_order_relaxed); }
	void setProgress(float p) const { m_progress.store(p, std::memory_order_relaxed); }
	bool isGenerating() const { return m_generating.load(std::memory_order_relaxed); }
	void setGenerating(bool g) const { m_generating.store(g, std::memory_order_relaxed); }

protected:
	std::atomic<uint64_t> m_version{1};
	mutable std::atomic<float> m_progress{0.0f};
	mutable std::atomic<bool> m_generating{false};
};
