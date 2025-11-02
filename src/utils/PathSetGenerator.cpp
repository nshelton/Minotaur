#include <cmath>
#include "PathSetGenerator.h"

namespace PathSetGenerator {

PathSet Circle(Vec2 center_mm, float radius_mm, int segments, Color col) {
    if (segments < 3) segments = 3;
    PathSet ps{};
    ps.color = col;
    Path path{}; path.closed = true;
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < segments; ++i) {
        float t = (float)i / (float)segments;
        float ang = t * twoPi;
        float x = center_mm.x + radius_mm * cosf(ang);
        float y = center_mm.y + radius_mm * sinf(ang);
        path.points.push_back(Vec2{x, y});
    }
    ps.paths.push_back(std::move(path));
    return ps;
}

PathSet Square(Vec2 center_mm, float side_mm, Color col) {
    PathSet ps{};
    ps.color = col;
    Path path{}; path.closed = true;
    float h = side_mm * 0.5f;
    path.points.push_back(center_mm + Vec2{-h, -h});
    path.points.push_back(center_mm + Vec2{ h, -h});
    path.points.push_back(center_mm + Vec2{ h,  h});
    path.points.push_back(center_mm + Vec2{-h,  h});
    ps.paths.push_back(std::move(path));
    return ps;
}

PathSet Star(Vec2 center_mm, float outer_radius_mm, float inner_radius_mm, int points, Color col) {
    if (points < 2) points = 2;
    PathSet ps{};
    ps.color = col;
    Path path{}; path.closed = true;
    const float pi = 3.14159265359f;
    int verts = points * 2;
    for (int i = 0; i < verts; ++i) {
        float t = (float)i / (float)verts;
        float ang = t * 2.0f * pi - pi * 0.5f; // start at top
        float rad = (i % 2 == 0) ? outer_radius_mm : inner_radius_mm;
        float x = center_mm.x + rad * cosf(ang);
        float y = center_mm.y + rad * sinf(ang);
        path.points.push_back(Vec2{x, y});
    }
    ps.paths.push_back(std::move(path));
    return ps;
}

}

