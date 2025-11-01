#pragma once
#include "core/core.h"

namespace PathSetGenerator {

// Circle centered at (cx, cy) with radius in mm
PathSet Circle(Vec2 center_mm, float radius_mm, int segments = 64,
               Color color = Color(0.9f, 0.2f, 0.2f, 1.0f));

// Square centered at (cx, cy) with side length in mm
PathSet Square(Vec2 center_mm, float side_mm,
               Color color = Color(0.2f, 0.7f, 0.9f, 1.0f));

// Star with given outer/inner radii and point count
PathSet Star(Vec2 center_mm, float outer_radius_mm, float inner_radius_mm,
             int points = 5,
             Color color = Color(0.95f, 0.8f, 0.2f, 1.0f));

}

