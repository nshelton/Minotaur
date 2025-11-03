#pragma once

struct Color {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};

    Color () = default;
    Color(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha) {}
};