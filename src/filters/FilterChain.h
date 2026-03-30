#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>

#include "Types.h"
#include "Filter.h"

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
        waitForEval();
    }

    // Copy: copy base only, do not copy filters, caches, or async state
    FilterChain(const FilterChain &other)
        : m_filters(), m_layers(), m_base(other.m_base), m_baseGen(other.m_baseGen) {}

    FilterChain &operator=(const FilterChain &other)
    {
        if (this != &other)
        {
            waitForEval();
            m_filters.clear();
            m_layers.clear();
            m_base = other.m_base;
            m_baseGen = other.m_baseGen;
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_completedOutput.reset();
        }
        return *this;
    }

    // Move: wait for eval to complete, then move all state
    FilterChain(FilterChain &&other) noexcept
        : FilterChain()
    {
        if (other.m_evalFuture.valid()) other.m_evalFuture.wait();
        m_filters = std::move(other.m_filters);
        m_layers = std::move(other.m_layers);
        m_enabled = std::move(other.m_enabled);
        m_base = std::move(other.m_base);
        m_baseGen = other.m_baseGen;
        m_completedOutput = std::move(other.m_completedOutput);
    }

    FilterChain &operator=(FilterChain &&other) noexcept
    {
        if (this != &other)
        {
            waitForEval();
            if (other.m_evalFuture.valid()) other.m_evalFuture.wait();
            m_filters = std::move(other.m_filters);
            m_layers = std::move(other.m_layers);
            m_enabled = std::move(other.m_enabled);
            m_base = std::move(other.m_base);
            m_baseGen = other.m_baseGen;
            m_completedOutput = std::move(other.m_completedOutput);
        }
        return *this;
    }

    void clear()
    {
        waitForEval();
        m_filters.clear();
        m_layers.clear();
        m_enabled.clear();
        m_base.reset();
        m_baseGen = 0;
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_completedOutput.reset();
    }

    void setBase(const LayerPtr &base, uint64_t baseGen)
    {
        waitForEval();
        m_base = base;
        m_baseGen = baseGen;
        // Invalidate all caches
        for (auto &lc : m_layers)
            lc.valid = false;
    }

    size_t addFilter(std::unique_ptr<FilterBase> f)
    {
        waitForEval();

        // Validate chain typing
        if (m_filters.empty())
        {
            if (m_base)
                assert(f->inputKind() == m_base->kind());
        }
        else
        {
            auto prevOut = m_filters.back()->outputKind();
            assert(prevOut == f->inputKind());
        }

        m_filters.emplace_back(std::move(f));
        m_layers.emplace_back(LayerCache{});
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

        if (!isEvaluating())
        {
            if (needsEval())
            {
                m_evalFuture = std::async(std::launch::async, [this]() {
                    evaluate(m_filters.size() - 1);
                    std::lock_guard<std::mutex> lock(m_resultMutex);
                    m_completedOutput = m_layers.back().data;
                });
            }
        }

        std::lock_guard<std::mutex> lock(m_resultMutex);
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
            evaluate(m_filters.size() - 1);
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_completedOutput = m_layers.back().data;
        }

        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_completedOutput ? m_completedOutput : m_base;
    }

    LayerPtr outputLayer() const
    {
        if (m_filters.empty())
            return m_base;
        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_completedOutput ? m_completedOutput : LayerPtr{};
    }

    void invalidateAll()
    {
        for (auto &lc : m_layers)
            lc.valid = false;
    }

    // Wait for any in-flight background evaluation to finish
    void waitForEval() const
    {
        if (m_evalFuture.valid()) m_evalFuture.wait();
    }

    // Returns true if a background evaluation is currently running
    bool isEvaluating() const
    {
        if (!m_evalFuture.valid()) return false;
        return m_evalFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
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
    const LayerCache *layerCacheAt(size_t i) const { return i < m_layers.size() ? &m_layers[i] : nullptr; }
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
        m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(index));
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
                return;
        }
        else
        {
            if (!canDisableFilterAtIndex(index))
                return;
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

    const LayerPtr &evaluate(size_t upTo) const
    {
        assert(upTo < m_filters.size());

        // Evaluate forward from 0 to upTo so each filter sees
        // the correct upstream gen after it has been updated.
        for (size_t i = 0; i <= upTo; ++i)
        {
            LayerCache &cache = m_layers[i];
            FilterBase &filter = *m_filters[i];
            uint64_t upstreamGen = (i == 0) ? m_baseGen : m_layers[i - 1].gen;

            if (!m_enabled[i])
            {
                if (cache.valid && cache.upstreamGen == upstreamGen)
                    continue;
                const LayerPtr &upstream = (i == 0) ? m_base : m_layers[i - 1].data;
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

            const LayerPtr &upstream = (i == 0) ? m_base : m_layers[i - 1].data;
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
        return m_layers[upTo].data;
    }

    std::vector<std::unique_ptr<FilterBase>> m_filters;
    mutable std::vector<LayerCache> m_layers;
    std::vector<bool> m_enabled;
    LayerPtr m_base;
    mutable uint64_t m_baseGen{0};

    // Background evaluation state
    mutable std::future<void> m_evalFuture;
    mutable std::mutex m_resultMutex;
    mutable LayerPtr m_completedOutput;
};
