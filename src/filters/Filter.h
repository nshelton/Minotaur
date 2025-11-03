#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <type_traits>
#include <atomic>

#include "filters/Types.h"

struct FilterParameter
{
    std::string name;
    float minValue{0.0f};
    float maxValue{1.0f};
    float value{0.5f};
};

// Base interface for all filters
struct FilterBase
{
    FilterBase() = default;
    virtual ~FilterBase() = default;

    // Descriptive name for UI/debugging
    virtual const char *name() const = 0;

    // Type information for safe chaining
    virtual LayerKind inputKind() const = 0;
    virtual LayerKind outputKind() const = 0;

    // Monotonic version counter that changes when parameters are updated
    virtual uint64_t paramVersion() const = 0;

    // Perform the filter operation. Implementations should expect 'in' to be of inputKind()
    // and must write to 'out' with a value of outputKind().
    virtual void apply(const LayerPtr &in, LayerPtr &out) const = 0;
    std::map<std::string, FilterParameter> m_parameters;

    void setParameter(const std::string &key, const float value)
    {
        auto it = m_parameters.find(key);
        if (it != m_parameters.end())
        {
            it->second.value = value;
            m_version.fetch_add(1);
        }
    }

    // Timing: last execution time in milliseconds
    double lastRunMs() const { return m_lastRunMs.load(); }
    void setLastRunMs(double ms) { m_lastRunMs.store(ms); }

protected:
    std::atomic<uint64_t> m_version{1};
    std::atomic<double> m_lastRunMs{0.0};
};

template <typename T>
inline void ensure(LayerPtr &p)
{
    if constexpr (std::is_same_v<T, Bitmap>)
    {
        if (!isBitmapLayer(p))
            p = std::make_shared<Bitmap>();
    }
    else
    {
        if (!isPathSetLayer(p))
            p = std::make_shared<PathSet>();
    }
}
template <typename T>
inline T &as(LayerPtr &p)
{
    if constexpr (std::is_same_v<T, Bitmap>)
    {
        assert(isBitmapLayer(p));
        return *static_cast<Bitmap *>(p.get());
    }
    else
    {
        assert(isPathSetLayer(p));
        return *static_cast<PathSet *>(p.get());
    }
}
template <typename T>
inline const T &asConst(const LayerPtr &p)
{
    if constexpr (std::is_same_v<T, Bitmap>)
    {
        assert(isBitmapLayer(p));
        return *static_cast<const Bitmap *>(p.get());
    }
    else
    {
        assert(isPathSetLayer(p));
        return *static_cast<const PathSet *>(p.get());
    }
}

// Strongly-typed convenience shim for implementing filters without manual variant handling
template <typename InT, typename OutT>
struct FilterTyped : public FilterBase
{
    const char *name() const override { return "UnnamedFilter"; }

    LayerKind inputKind() const override
    {
        if constexpr (std::is_same<InT, Bitmap>::value)
            return LayerKind::Bitmap;
        else
            return LayerKind::PathSet;
    }

    LayerKind outputKind() const override
    {
        if constexpr (std::is_same<OutT, Bitmap>::value)
            return LayerKind::Bitmap;
        else
            return LayerKind::PathSet;
    }

    void apply(const LayerPtr &in, LayerPtr &out) const override
    {
        const InT &src = asConst<InT>(in);
        ensure<OutT>(out);
        OutT &dst = as<OutT>(out);
        applyTyped(src, dst);
    }

    // Implemented by concrete filters
    virtual void applyTyped(const InT &in, OutT &out) const = 0;
};
