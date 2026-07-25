#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <cassert>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "Types.h"
#include "Filter.h"

inline const char *layerKindName(LayerKind k)
{
	switch (k)
	{
	case LayerKind::Bitmap: return "Bitmap";
	case LayerKind::PathSet: return "PathSet";
	case LayerKind::FloatImage: return "FloatImage";
	case LayerKind::ColorImage: return "ColorImage";
	}
	return "Unknown";
}

struct LayerCache
{
	LayerPtr data;
	uint64_t upstreamGen{0};
	uint64_t paramVer{0};
	uint64_t gen{0};
	bool valid{false};
};

class FilterChain
{
public:
	FilterChain() = default;

	~FilterChain()
	{
		stopWorker();
	}

	// Copy: copy base only, do not copy filters, caches, or async state
	FilterChain(const FilterChain &other)
		: m_filters(), m_layers(), m_base(other.m_base), m_baseGen(other.m_baseGen) {}

	FilterChain &operator=(const FilterChain &other)
	{
		if (this != &other)
		{
			stopWorker();
			m_filters.clear();
			{
				std::lock_guard<std::mutex> lock(m_dataMutex);
				m_layers.clear();
				m_completedOutput.reset();
			}
			m_enabled.clear();
			m_base = other.m_base;
			m_baseGen = other.m_baseGen;
		}
		return *this;
	}

	// Move: stop workers, then move all state
	FilterChain(FilterChain &&other) noexcept
		: FilterChain()
	{
		other.stopWorker();
		m_filters = std::move(other.m_filters);
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			std::lock_guard<std::mutex> otherLock(other.m_dataMutex);
			m_layers = std::move(other.m_layers);
			m_completedOutput = std::move(other.m_completedOutput);
		}
		m_enabled = std::move(other.m_enabled);
		m_base = std::move(other.m_base);
		m_baseGen = other.m_baseGen;
	}

	FilterChain &operator=(FilterChain &&other) noexcept
	{
		if (this != &other)
		{
			stopWorker();
			other.stopWorker();
			m_filters = std::move(other.m_filters);
			{
				std::lock_guard<std::mutex> lock(m_dataMutex);
				std::lock_guard<std::mutex> otherLock(other.m_dataMutex);
				m_layers = std::move(other.m_layers);
				m_completedOutput = std::move(other.m_completedOutput);
			}
			m_enabled = std::move(other.m_enabled);
			m_base = std::move(other.m_base);
			m_baseGen = other.m_baseGen;
		}
		return *this;
	}

	void clear()
	{
		stopWorker();
		m_filters.clear();
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			m_layers.clear();
			m_completedOutput.reset();
		}
		m_enabled.clear();
		m_base.reset();
		m_baseGen = 0;
	}

	void setBase(const LayerPtr &base, uint64_t baseGen)
	{
		waitForEval();
		m_base = base;
		m_baseGen = baseGen;
		// Invalidate all caches
		std::lock_guard<std::mutex> lock(m_dataMutex);
		for (auto &lc : m_layers)
			lc.valid = false;
	}

	// Returns the index of the appended filter, or SIZE_MAX if the filter's
	// input kind is incompatible with the upstream output (nothing is added).
	size_t addFilter(std::unique_ptr<FilterBase> f)
	{
		waitForEval();

		// Validate chain typing (release-safe: asserts compile out, so guard at runtime)
		LayerKind upstreamKind;
		bool haveUpstream;
		if (m_filters.empty())
		{
			haveUpstream = static_cast<bool>(m_base);
			upstreamKind = m_base ? m_base->kind() : LayerKind::PathSet;
		}
		else
		{
			haveUpstream = true;
			upstreamKind = m_filters.back()->outputKind();
		}

		if (haveUpstream && f->inputKind() != upstreamKind)
		{
			LOG(ERROR) << "addFilter: type mismatch for \"" << f->name()
			           << "\" expected input " << layerKindName(upstreamKind)
			           << " but filter accepts " << layerKindName(f->inputKind())
			           << "; not adding";
			return static_cast<size_t>(-1);
		}

		m_filters.emplace_back(std::move(f));
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			m_layers.emplace_back(LayerCache{});
		}
		m_enabled.emplace_back(true);
		return m_filters.size() - 1;
	}

	size_t size() const { return m_filters.size(); }

	// Non-blocking output: returns last completed result.
	// Kicks off background evaluation if caches are stale.
	LayerPtr output() const
	{
		if (m_filters.empty())
			return m_base;

		if (!m_evaluating.load(std::memory_order_acquire))
		{
			if (needsEval())
			{
				requestEval();
			}
		}

		std::lock_guard<std::mutex> lock(m_dataMutex);
		return m_completedOutput ? m_completedOutput : m_base;
	}

	// Blocking output: waits for evaluation to complete.
	// Use when you need the definitive result (e.g. plotting).
	LayerPtr outputBlocking() const
	{
		if (m_filters.empty())
			return m_base;

		// If an eval is in flight, wait for it
		waitForEval();

		// If still stale (e.g. params changed during wait), evaluate synchronously
		if (needsEval())
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			evaluateInto(m_layers, m_filters.size() - 1);
			m_completedOutput = m_layers.back().data;
		}

		std::lock_guard<std::mutex> lock(m_dataMutex);
		return m_completedOutput ? m_completedOutput : m_base;
	}

	LayerPtr outputLayer() const
	{
		if (m_filters.empty())
			return m_base;
		std::lock_guard<std::mutex> lock(m_dataMutex);
		return m_completedOutput ? m_completedOutput : LayerPtr{};
	}

	void invalidateAll()
	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		for (auto &lc : m_layers)
			lc.valid = false;
	}

	// Wait for any in-flight background evaluation to finish
	void waitForEval() const
	{
		std::unique_lock<std::mutex> lock(m_workerMutex);
		m_workerCV.wait(lock, [this]() {
			return !m_evaluating.load(std::memory_order_relaxed) &&
			       !m_evalRequested.load(std::memory_order_relaxed);
		});
	}

	// Returns true if a background evaluation is currently running
	bool isEvaluating() const
	{
		return m_evaluating.load(std::memory_order_acquire);
	}

	// Returns the index of the filter currently being evaluated, or -1 if none
	int activeFilterIndex() const
	{
		for (size_t i = 0; i < m_filters.size(); ++i)
		{
			if (m_filters[i]->isRunning()) return static_cast<int>(i);
		}
		return -1;
	}

	// Debug/inspection accessors (read-only)
	size_t filterCount() const { return m_filters.size(); }
	FilterBase *filterAt(size_t i) const { return i < m_filters.size() ? m_filters[i].get() : nullptr; }

	// Returns a snapshot of the layer cache (thread-safe copy)
	LayerCache layerCacheAt(size_t i) const
	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		return i < m_layers.size() ? m_layers[i] : LayerCache{};
	}

	uint64_t baseGen() const { return m_baseGen; }
	LayerKind baseKind() const { return m_base ? m_base->kind() : LayerKind::PathSet; }

	// Modification: remove a filter by index
	bool removeFilter(size_t index)
	{
		waitForEval();

		if (index >= m_filters.size())
			return false;

		if (!canRemoveFilterAtIndex(index))
			return false;

		m_filters.erase(m_filters.begin() + static_cast<std::ptrdiff_t>(index));
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(index));
		}
		m_enabled.erase(m_enabled.begin() + static_cast<std::ptrdiff_t>(index));
		// Invalidate remaining caches; upstream may have changed
		invalidateAll();
		return true;
	}

	// Enable/disable a filter (bypass when disabled)
	void setFilterEnabled(size_t index, bool enabled)
	{
		waitForEval();

		if (index >= m_enabled.size())
			return;
		if (m_enabled[index] == enabled)
			return;

		if (enabled)
		{
			if (!canEnableFilterAtIndex(index))
			{
				FilterBase *f = m_filters[index].get();
				LOG(ERROR) << "setFilterEnabled: enabling \"" << f->name()
				           << "\" (in " << layerKindName(f->inputKind())
				           << " -> out " << layerKindName(f->outputKind())
				           << ") would break chain typing; leaving disabled";
				return;
			}
		}
		else
		{
			if (!canDisableFilterAtIndex(index))
			{
				FilterBase *f = m_filters[index].get();
				LOG(ERROR) << "setFilterEnabled: disabling \"" << f->name()
				           << "\" would leave a downstream consumer with an incompatible upstream kind; leaving enabled";
				return;
			}
		}

		m_enabled[index] = enabled;
		invalidateAll();
	}

	bool isFilterEnabled(size_t index) const
	{
		return (index < m_enabled.size()) ? m_enabled[index] : false;
	}

private:
	// Find previous enabled filter index before 'start' (exclusive). Returns -1 if none.
	int prevEnabledIndex(int start) const
	{
		for (int j = start; j >= 0; --j)
		{
			if (j < static_cast<int>(m_enabled.size()) && m_enabled[static_cast<size_t>(j)])
				return j;
		}
		return -1;
	}

	// Find next enabled filter index at or after 'start'. Returns -1 if none.
	int nextEnabledIndex(int start) const
	{
		for (int j = start; j < static_cast<int>(m_enabled.size()); ++j)
		{
			if (m_enabled[static_cast<size_t>(j)])
				return j;
		}
		return -1;
	}

public:
	// Check if removing (or equivalently bypassing) filter at index preserves type compatibility
	bool canRemoveFilterAtIndex(size_t index) const
	{
		if (index >= m_filters.size()) return false;

		// Upstream kind is from previous enabled filter, otherwise base kind
		int prevEn = prevEnabledIndex(static_cast<int>(index) - 1);
		LayerKind upstreamKind = (prevEn >= 0)
		                             ? m_filters[static_cast<size_t>(prevEn)]->outputKind()
		                             : baseKind();

		// Downstream is next enabled filter after index
		int nextEn = nextEnabledIndex(static_cast<int>(index) + 1);
		if (nextEn < 0) return true; // no downstream consumer

		LayerKind nextInput = m_filters[static_cast<size_t>(nextEn)]->inputKind();
		return upstreamKind == nextInput;
	}

	bool canEnableFilterAtIndex(size_t index) const
	{
		if (index >= m_filters.size()) return false;

		// Upstream enabled before index
		int prevEn = prevEnabledIndex(static_cast<int>(index) - 1);
		LayerKind upstreamKind = (prevEn >= 0)
		                             ? m_filters[static_cast<size_t>(prevEn)]->outputKind()
		                             : baseKind();

		// This filter must accept upstream kind
		if (m_filters[index]->inputKind() != upstreamKind)
			return false;

		// And its output must feed next enabled filter if exists
		int nextEn = nextEnabledIndex(static_cast<int>(index) + 1);
		if (nextEn < 0) return true;
		return m_filters[index]->outputKind() == m_filters[static_cast<size_t>(nextEn)]->inputKind();
	}

	bool canDisableFilterAtIndex(size_t index) const
	{
		return canRemoveFilterAtIndex(index);
	}

	// Quick check whether any cache in the chain is stale (does not recurse/recompute)
	bool needsEval() const
	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		for (size_t i = 0; i < m_filters.size(); ++i)
		{
			if (!m_enabled[i]) continue;
			const LayerCache &cache = m_layers[i];
			uint64_t upGen = (i == 0) ? m_baseGen : m_layers[i - 1].gen;
			if (!cache.valid ||
				cache.upstreamGen != upGen ||
				cache.paramVer != m_filters[i]->paramVersion())
				return true;
		}
		return false;
	}

	// Evaluate filter chain into the given layer cache vector.
	// Caller must ensure exclusive access to 'layers'.
	// m_filters, m_enabled, m_base, m_baseGen must be stable (not mutated concurrently).
	const LayerPtr &evaluateInto(std::vector<LayerCache> &layers, size_t upTo) const
	{
		assert(upTo < m_filters.size());

		// Evaluate forward from 0 to upTo so each filter sees
		// the correct upstream gen after it has been updated.
		for (size_t i = 0; i <= upTo; ++i)
		{
			LayerCache &cache = layers[i];
			FilterBase &filter = *m_filters[i];
			uint64_t upstreamGen = (i == 0) ? m_baseGen : layers[i - 1].gen;

			if (!m_enabled[i])
			{
				if (cache.valid && cache.upstreamGen == upstreamGen)
					continue;
				const LayerPtr &upstream = (i == 0) ? m_base : layers[i - 1].data;
				cache.data = upstream;
				cache.upstreamGen = upstreamGen;
				cache.paramVer = filter.paramVersion();
				cache.gen = upstreamGen; // propagate generation for downstream
				cache.valid = true;
				continue;
			}

			bool needsRecompute = !cache.valid ||
			                      (cache.upstreamGen != upstreamGen) ||
			                      (cache.paramVer != filter.paramVersion());

			if (!needsRecompute) continue;

			const LayerPtr &upstream = (i == 0) ? m_base : layers[i - 1].data;
			auto t0 = std::chrono::high_resolution_clock::now();
			// Avoid aliasing: if our output pointer aliases upstream, reset so we don't mutate upstream in-place
			if (cache.data == upstream)
			{
				cache.data.reset();
			}

			filter.setRunning(true);
			filter.setProgress(0.0f);
			filter.apply(upstream, cache.data);
			filter.setRunning(false);

			auto t1 = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double, std::milli> dt = t1 - t0;
			filter.setLastRunMs(dt.count());
			cache.upstreamGen = upstreamGen;
			cache.paramVer = filter.paramVersion();
			cache.gen = cache.gen + 1; // advance local generation
			cache.valid = true;

			if (cache.data.get()->kind() == LayerKind::PathSet)
			{
				const PathSet *psp = static_cast<const PathSet *>(cache.data.get());
				filter.setLastPathCount(psp->paths.size());
				int totalVerts = 0;
				for (auto path : psp->paths)
				{
					totalVerts += path.points.size();
				}
				filter.setLastVertexCount(totalVerts);
				LOG(INFO) << totalVerts << " "  <<  psp->paths.size();
			}
			LOG(INFO) << "recomputed " << filter.name();
		}
		return layers[upTo].data;
	}

	std::vector<std::unique_ptr<FilterBase>> m_filters;
	mutable std::vector<LayerCache> m_layers;
	std::vector<bool> m_enabled;

	// THREADING INVARIANT for m_base / m_baseGen:
	// These are NOT guarded by m_dataMutex. The background worker reads them in
	// evaluateInto() without any lock. That is race-free ONLY because they are
	// mutated solely by setBase() (and clear()), each of which first calls
	// waitForEval() to ensure m_evaluating == false before touching them. The
	// worker only runs while m_evaluating == true, so a write and a worker read
	// can never overlap. If you ever mutate m_base/m_baseGen from anywhere that
	// does not waitForEval() first, you MUST add explicit synchronization here.
	LayerPtr m_base;
	mutable uint64_t m_baseGen{0};

private:
	// Request background evaluation (starts worker if needed)
	void requestEval() const
	{
		std::lock_guard<std::mutex> lock(m_workerMutex);
		if (!m_workerThread.joinable())
		{
			m_stopWorker.store(false, std::memory_order_relaxed);
			m_workerThread = std::thread([this]() { workerLoop(); });
		}
		m_evalRequested.store(true, std::memory_order_relaxed);
		m_workerCV.notify_one();
	}

	// Stop the worker thread and join it
	void stopWorker() const
	{
		{
			std::lock_guard<std::mutex> lock(m_workerMutex);
			m_stopWorker.store(true, std::memory_order_relaxed);
		}
		m_workerCV.notify_all();
		if (m_workerThread.joinable())
			m_workerThread.join();
	}

	// Persistent worker thread loop
	void workerLoop() const
	{
		while (true)
		{
			{
				std::unique_lock<std::mutex> lock(m_workerMutex);
				m_workerCV.wait(lock, [this]() {
					return m_stopWorker.load(std::memory_order_relaxed) ||
					       m_evalRequested.load(std::memory_order_relaxed);
				});

				if (m_stopWorker.load(std::memory_order_relaxed))
					break;

				// Transition: clear request, mark evaluating (under lock for waitForEval)
				m_evalRequested.store(false, std::memory_order_relaxed);
				m_evaluating.store(true, std::memory_order_relaxed);
			}

			// Snapshot layer caches for isolated evaluation
			std::vector<LayerCache> workerLayers;
			{
				std::lock_guard<std::mutex> lock(m_dataMutex);
				workerLayers = m_layers;
			}

			// Run the evaluation on our private copy.
			// evaluateInto() reads m_base/m_baseGen WITHOUT m_dataMutex; this is
			// safe only because setBase()/clear() mutate them while m_evaluating
			// is false (see THREADING INVARIANT at the m_base declaration).
			evaluateInto(workerLayers, m_filters.size() - 1);

			// If a new eval was requested during our work, publish anyway
			// (the caches are valid for the params they were computed with)
			// then the loop will pick up the new request immediately.

			// Publish results back
			LayerPtr result = workerLayers.back().data;
			{
				std::lock_guard<std::mutex> lock(m_dataMutex);
				m_layers = std::move(workerLayers);
				m_completedOutput = result;
			}

			// Signal completion (under lock for consistent waitForEval predicate)
			{
				std::lock_guard<std::mutex> lock(m_workerMutex);
				m_evaluating.store(false, std::memory_order_relaxed);
			}
			m_workerCV.notify_all();

			// Loop continues: if m_evalRequested is true, wait() returns immediately
		}
	}

	// Data mutex: protects m_layers and m_completedOutput
	mutable std::mutex m_dataMutex;
	mutable LayerPtr m_completedOutput;

	// Worker thread synchronization
	mutable std::thread m_workerThread;
	mutable std::mutex m_workerMutex;
	mutable std::condition_variable m_workerCV;
	mutable std::atomic<bool> m_evalRequested{false};
	mutable std::atomic<bool> m_evaluating{false};
	mutable std::atomic<bool> m_stopWorker{false};
};
