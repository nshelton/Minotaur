#pragma once

#include <memory>
#include <string>
#include <variant>
#include "core/Pathset.h"
#include "core/Bitmap.h"
#include "core/FloatImage.h"
#include "core/ColorImage.h"
#include "filters/FilterChain.h"
#include "generators/GeneratorBase.h"

enum class EntityType
{
    PathSet,
    Bitmap,
    FloatImage,
    ColorImage
};

struct Entity
{
    int id;
    std::string name;
    std::variant<PathSet, Bitmap, FloatImage, ColorImage> payload;
    uint64_t payloadVersion{1};
    bool visible{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    // takes a point from local entity space (mm) to page space (mm)
    Mat3 localToPage;

    EntityType type() const
    {
        if (std::holds_alternative<PathSet>(payload)) return EntityType::PathSet;
        if (std::holds_alternative<Bitmap>(payload)) return EntityType::Bitmap;
        if (std::holds_alternative<FloatImage>(payload)) return EntityType::FloatImage;
        return EntityType::ColorImage;
    }

    BoundingBox boundsLocal() const
    {
        if (auto ps = std::get_if<PathSet>(&payload))
        {
            ps->computeAABB();
            return ps->aabb;
        }
        if (auto bmp = std::get_if<Bitmap>(&payload))
        {
            return bmp->aabb();
        }
        if (auto fi = std::get_if<FloatImage>(&payload))
        {
            return fi->aabb();
        }
        const ColorImage &ci = std::get<ColorImage>(payload);
        return ci.aabb();
    }

    bool contains(const Vec2 &point, float margin_mm = 0) const
    {
        return boundsLocal().contains(localToPage / point, margin_mm);
    }

    const PathSet *pathset() const { return std::get_if<PathSet>(&payload); }
    PathSet *pathset() { return std::get_if<PathSet>(&payload); }
    const Bitmap *bitmap() const { return std::get_if<Bitmap>(&payload); }
    Bitmap *bitmap() { return std::get_if<Bitmap>(&payload); }
    const FloatImage *floatImage() const { return std::get_if<FloatImage>(&payload); }
    FloatImage *floatImage() { return std::get_if<FloatImage>(&payload); }
    const ColorImage *colorImage() const { return std::get_if<ColorImage>(&payload); }
    ColorImage *colorImage() { return std::get_if<ColorImage>(&payload); }

    // Filter chain: transforms from base payload to display/output layer
    FilterChain filterChain;

    // Optional generator that produces the base payload parametrically.
    // Null for entities loaded from file (static payload).
    std::unique_ptr<GeneratorBase> generator;

    // Helper to package current payload into a LayerPtr
    LayerPtr baseLayer() const
    {
        if (auto ps = std::get_if<PathSet>(&payload))
            return makeLayerFrom(*ps);
        if (auto bmp = std::get_if<Bitmap>(&payload))
            return makeLayerFrom(*bmp);
        if (auto fi = std::get_if<FloatImage>(&payload))
            return makeLayerFrom(*fi);
        const ColorImage &ci = std::get<ColorImage>(payload);
        return makeLayerFrom(ci);
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

    // Unpack a completed LayerPtr into the payload variant and refresh the
    // filter chain. Used by both the sync and async paths.
    void applyGeneratorResult(const LayerPtr &out)
    {
        if (!out) return;
        if (isPathSetLayer(out))
            payload = *asPathSetPtr(out);
        else if (isBitmapLayer(out))
            payload = *asBitmapPtr(out);
        else if (isFloatImageLayer(out))
            payload = *asFloatImagePtr(out);
        else if (isColorImageLayer(out))
            payload = *asColorImagePtr(out);

        payloadVersion++;
        refreshFilterBase();
    }
};