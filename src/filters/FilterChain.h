#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <cassert>
#include <chrono>

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

    // Copy: copy base only, do not copy filters or caches
    FilterChain(const FilterChain &other)
        : m_filters(), m_layers(), m_base(other.m_base), m_baseGen(other.m_baseGen) {}

    FilterChain &operator=(const FilterChain &other)
    {
        if (this != &other)
        {
            m_filters.clear();
            m_layers.clear();
            m_base = other.m_base;
            m_baseGen = other.m_baseGen;
        }
        return *this;
    }

    // Move defaulted
    FilterChain(FilterChain &&) noexcept = default;
    FilterChain &operator=(FilterChain &&) noexcept = default;

    void clear()
    {
        m_filters.clear();
        m_layers.clear();
        m_enabled.clear();
        m_base.reset();
        m_baseGen = 0;
    }

    void setBase(const LayerPtr &base, uint64_t baseGen)
    {
        m_base = base;
        m_baseGen = baseGen;
        // Invalidate all caches
        for (auto &lc : m_layers)
            lc.valid = false;
    }

    size_t addFilter(std::unique_ptr<FilterBase> f)
    {
        // Validate chain typing
        if (m_filters.empty())
        {
            if (m_base && m_base->kind() == LayerKind::Bitmap)
                assert(f->inputKind() == LayerKind::Bitmap);
            else if (m_base && m_base->kind() == LayerKind::PathSet)
                assert(f->inputKind() == LayerKind::PathSet);
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

    const LayerPtr &output()
    {
        if (m_filters.empty())
            return m_base;
        return evaluate(m_filters.size() - 1);
    }

    void invalidateAll()
    {
        for (auto &lc : m_layers)
            lc.valid = false;
    }

    // Debug/inspection accessors (read-only)
    size_t filterCount() const { return m_filters.size(); }
    FilterBase *filterAt(size_t i) const { return i < m_filters.size() ? m_filters[i].get() : nullptr; }
    const LayerCache *layerCacheAt(size_t i) const { return i < m_layers.size() ? &m_layers[i] : nullptr; }
    uint64_t baseGen() const { return m_baseGen; }
    LayerKind baseKind() const { return (m_base && m_base->kind() == LayerKind::Bitmap) ? LayerKind::Bitmap : LayerKind::PathSet; }

    // Modification: remove a filter by index
    bool removeFilter(size_t index)
    {
        if (index >= m_filters.size()) return false;
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
        if (index >= m_enabled.size()) return;
        if (m_enabled[index] != enabled)
        {
            m_enabled[index] = enabled;
            invalidateAll();
        }
    }

    bool isFilterEnabled(size_t index) const
    {
        return (index < m_enabled.size()) ? m_enabled[index] : false;
    }

private:
    const LayerPtr &evaluate(size_t i)
    {
        assert(i < m_filters.size());
        // Ensure upstream is evaluated so its cache.data is valid
        const LayerPtr &upstream = (i == 0) ? m_base : evaluate(i - 1);
        uint64_t upstreamGen = (i == 0) ? m_baseGen : m_layers[i - 1].gen;

        LayerCache &cache = m_layers[i];
        FilterBase &filter = *m_filters[i];

        if (!m_enabled[i])
        {
            // Bypass: forward upstream unchanged
            cache.data = upstream;
            cache.upstreamGen = upstreamGen;
            cache.paramVer = filter.paramVersion();
            cache.gen = upstreamGen; // propagate generation for downstream
            cache.valid = true;
            return cache.data;
        }

        bool needsRecompute = !cache.valid ||
                              (cache.upstreamGen != upstreamGen) ||
                              (cache.paramVer != filter.paramVersion());

        if (needsRecompute)
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            // Avoid aliasing: if our output pointer aliases upstream, reset so we don't mutate upstream in-place
            if (cache.data == upstream)
            {
                cache.data.reset();
            }
            filter.apply(upstream, cache.data);
            auto t1 = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> dt = t1 - t0;
            filter.setLastRunMs(dt.count());
            cache.upstreamGen = upstreamGen;
            cache.paramVer = filter.paramVersion();
            cache.gen = cache.gen + 1; // advance local generation
            cache.valid = true;
        }
        return cache.data;
    }

    std::vector<std::unique_ptr<FilterBase>> m_filters;
    std::vector<LayerCache> m_layers;
    std::vector<bool> m_enabled;
    LayerPtr m_base;
    uint64_t m_baseGen{0};
};
