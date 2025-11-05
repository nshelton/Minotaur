#pragma once

#include <map>
#include "core/core.h"

struct PageModel
{
    void addPathSet(const PathSet& ps);
    void addBitmap(const Bitmap& bm);
    int duplicateEntity(int sourceId);

    // Dimensions in millimeters (ISO 216): A3 = 297 x 420
    const float page_width_mm = 297.0f;
    const float page_height_mm = 420.0f;

    Vec2 mouse_pixel;
    Vec2 mouse_page_mm;
    std::map<int, Entity> entities;

};
