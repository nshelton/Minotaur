#pragma once

#include <memory>
#include <string>
#include "core/Pathset.h"
#include "core/Bitmap.h"
#include "core/FloatImage.h"
#include "core/ColorImage.h"
#include "filters/FilterChain.h"
#include "generators/GeneratorBase.h"

struct Entity
{
    int id;
    std::string name;
    LayerPtr payload;
    uint64_t payloadVersion{1};
    bool visible{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    // takes a point from local entity space (mm) to page space (mm)
    Mat3 localToPage;

    LayerKind type() const
    {
        return payload ? payload->kind() : LayerKind::PathSet;
    }

    BoundingBox boundsLocal() const
    {
        if (!payload) return BoundingBox{};
        switch (payload->kind())
        {
        case LayerKind::PathSet:
        {
            auto *ps = asPathSetPtr(payload);
            ps->computeAABB();
            return ps->aabb;
        }
        case LayerKind::Bitmap:
            return asBitmapConstPtr(payload)->aabb();
        case LayerKind::FloatImage:
            return asFloatImageConstPtr(payload)->aabb();
        case LayerKind::ColorImage:
            return asColorImageConstPtr(payload)->aabb();
        }
        return BoundingBox{};
    }

    bool contains(const Vec2 &point, float margin_mm = 0) const
    {
        return boundsLocal().contains(localToPage / point, margin_mm);
    }

    const PathSet *pathset() const { return asPathSetConstPtr(payload); }
    PathSet *pathset() { return asPathSetPtr(payload); }
    const Bitmap *bitmap() const { return asBitmapConstPtr(payload); }
    Bitmap *bitmap() { return asBitmapPtr(payload); }
    const FloatImage *floatImage() const { return asFloatImageConstPtr(payload); }
    FloatImage *floatImage() { return asFloatImagePtr(payload); }
    const ColorImage *colorImage() const { return asColorImageConstPtr(payload); }
    ColorImage *colorImage() { return asColorImagePtr(payload); }

    // Filter chain: transforms from base payload to display/output layer
    FilterChain filterChain;

    // Optional generator that produces the base payload parametrically.
    // Null for entities loaded from file (static payload).
    std::unique_ptr<GeneratorBase> generator;

    // Helper to package current payload into a LayerPtr for the filter chain base.
    // Returns a copy so the filter chain owns its own data.
    LayerPtr baseLayer() const
    {
        if (!payload) return nullptr;
        switch (payload->kind())
        {
        case LayerKind::PathSet:
            return makeLayerFrom(*asPathSetConstPtr(payload));
        case LayerKind::Bitmap:
            return makeLayerFrom(*asBitmapConstPtr(payload));
        case LayerKind::FloatImage:
            return makeLayerFrom(*asFloatImageConstPtr(payload));
        case LayerKind::ColorImage:
            return makeLayerFrom(*asColorImageConstPtr(payload));
        }
        return nullptr;
    }

    void refreshFilterBase()
    {
        filterChain.setBase(baseLayer(), payloadVersion);
    }

    // Call once per frame.
    // - Synchronous generators: runs generate() immediately when params are stale.
    // - Async generators: calls startGenerate() when params are stale, then
    //   returns. Call pollAsyncGenerator() each frame to check for completion.
    void tickGenerator()
    {
        if (!generator) return;
        if (generator->paramVersion() == m_lastGenVer) return;

        m_lastGenVer = generator->paramVersion();

        if (generator->isAsync())
        {
            generator->startGenerate();
            // Result will be collected by pollAsyncGenerator() when ready
        }
        else
        {
            LayerPtr out;
            generator->generate(out);
            applyGeneratorResult(out);
        }
    }

    // Call once per frame after tickGenerator() for entities with async generators.
    // When the background job finishes, transfers the result into the payload
    // and invalidates the filter chain. No-op for synchronous generators.
    void pollAsyncGenerator()
    {
        if (!generator || !generator->isAsync()) return;
        if (!generator->isReady()) return;

        LayerPtr out;
        generator->collectResult(out);
        applyGeneratorResult(out);
    }

private:
    uint64_t m_lastGenVer{0};

    // Transfer a completed LayerPtr into the payload and refresh the filter chain.
    void applyGeneratorResult(const LayerPtr &out)
    {
        if (!out) return;
        payload = out;
        payloadVersion++;
        refreshFilterBase();
    }
};
