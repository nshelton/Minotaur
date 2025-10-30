#include <cmath>
#include "PathSetGenerator.h"

namespace PathSetGenerator {

PathSet Circle(float cx_mm, float cy_mm, float radius_mm, int segments,
               float r, float g, float b, float a) {
    if (segments < 3) segments = 3;
    PathSet ps{};
    ps.transform = Transform2D{}; // identity in page space
    ps.r = r; ps.g = g; ps.b = b; ps.a = a;
    Path path{}; path.closed = true;
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < segments; ++i) {
        float t = (float)i / (float)segments;
        float ang = t * twoPi;
        float x = cx_mm + radius_mm * cosf(ang);
        float y = cy_mm + radius_mm * sinf(ang);
        path.points.push_back(Vec2{x, y});
    }
    ps.paths.push_back(std::move(path));
    return ps;
}

PathSet Square(float cx_mm, float cy_mm, float side_mm,
               float r, float g, float b, float a) {
    PathSet ps{};
    ps.transform = Transform2D{};
    ps.r = r; ps.g = g; ps.b = b; ps.a = a;
    Path path{}; path.closed = true;
    float h = side_mm * 0.5f;
    path.points.push_back(Vec2{cx_mm - h, cy_mm - h});
    path.points.push_back(Vec2{cx_mm + h, cy_mm - h});
    path.points.push_back(Vec2{cx_mm + h, cy_mm + h});
    path.points.push_back(Vec2{cx_mm - h, cy_mm + h});
    ps.paths.push_back(std::move(path));
    return ps;
}

PathSet Star(float cx_mm, float cy_mm, float outer_radius_mm, float inner_radius_mm,
             int points, float r, float g, float b, float a) {
    if (points < 2) points = 2;
    PathSet ps{};
    ps.transform = Transform2D{};
    ps.r = r; ps.g = g; ps.b = b; ps.a = a;
    Path path{}; path.closed = true;
    const float pi = 3.14159265359f;
    int verts = points * 2;
    for (int i = 0; i < verts; ++i) {
        float t = (float)i / (float)verts;
        float ang = t * 2.0f * pi - pi * 0.5f; // start at top
        float rad = (i % 2 == 0) ? outer_radius_mm : inner_radius_mm;
        float x = cx_mm + rad * cosf(ang);
        float y = cy_mm + rad * sinf(ang);
        path.points.push_back(Vec2{x, y});
    }
    ps.paths.push_back(std::move(path));
    return ps;
}

}

