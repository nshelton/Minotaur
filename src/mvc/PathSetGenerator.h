#pragma once

#include "Model.h"

namespace PathSetGenerator {

// Circle centered at (cx, cy) with radius in mm
PathSet Circle(float cx_mm, float cy_mm, float radius_mm, int segments = 64,
               float r = 0.9f, float g = 0.2f, float b = 0.2f, float a = 1.0f);

// Square centered at (cx, cy) with side length in mm
PathSet Square(float cx_mm, float cy_mm, float side_mm,
               float r = 0.2f, float g = 0.7f, float b = 0.9f, float a = 1.0f);

// Star with given outer/inner radii and point count
PathSet Star(float cx_mm, float cy_mm, float outer_radius_mm, float inner_radius_mm,
             int points = 5,
             float r = 0.95f, float g = 0.8f, float b = 0.2f, float a = 1.0f);

}

