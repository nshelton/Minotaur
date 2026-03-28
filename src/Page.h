#pragma once

#include <map>
#include <memory>
#include "core/Core.h"
#include "generators/GeneratorBase.h"

struct PageModel
{
    int addPathSet(const PathSet& ps);
    void addBitmap(const Bitmap& bm);
    void addColorImage(const ColorImage& ci);

    // Create an entity driven by a generator. The generator is immediately
    // ticked to populate the payload. Returns the new entity id.
    // positionMm sets the initial localToPage translation (default: origin).
    int addGeneratedEntity(std::unique_ptr<GeneratorBase> gen,
                           Vec2 positionMm = Vec2{0.0f, 0.0f});

    int duplicateEntity(int sourceId);

    // Dimensions in millimeters (ISO 216): A3 = 297 x 420
    const float page_width_mm = 297.0f;
    const float page_height_mm = 420.0f;

    Vec2 mouse_pixel;
    Vec2 mouse_page_mm;
    std::map<int, Entity> entities;

};
