#pragma once

#include <vector>

struct Vec2 {
    float x{0.0f};
    float y{0.0f};
};

struct Transform2D {
    float tx{0.0f};
    float ty{0.0f};
    float scale{1.0f};
};

struct Path {
    std::vector<Vec2> points;
    bool closed{false};
};

struct PathSet {
    Transform2D transform{}; // page-space transform (mm)
    std::vector<Path> paths;
    float r{1.0f}, g{1.0f}, b{1.0f}, a{1.0f};
};

struct PlotModel {
    std::vector<PathSet> entities;
};

