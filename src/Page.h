#pragma once

#include <map>
#include <memory>
#include <string>
#include "core/Core.h"
#include "generators/GeneratorBase.h"

struct PageModel
{
    template <typename T>
    int addEntity(const T &data, const std::string &name = "");

    // Create an entity driven by a generator. The generator is immediately
    // ticked to populate the payload. Returns the new entity id.
    // positionMm sets the initial localToPage translation (default: origin).
    int addGeneratedEntity(std::unique_ptr<GeneratorBase> gen,
                           Vec2 positionMm = Vec2{0.0f, 0.0f});

    int duplicateEntity(int sourceId);

    // Dimensions in millimeters (ISO 216): A3 = 297 x 420
    const float page_width_mm = 297.0f;
    const float page_height_mm = 420.0f;

    std::map<int, Entity> entities;

};
