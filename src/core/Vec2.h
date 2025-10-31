#pragma once

struct Vec2
{
    float x;
    float y;

    Vec2(float x, float y) : x(x), y(y) {}
    Vec2()
    {
        x = 0.0f;
        y = 0.0f;
    }

    inline Vec2 operator+(const Vec2 &other) const { return Vec2{x + other.x, y + other.y}; }

    inline Vec2 operator-(const Vec2 &other) const { return Vec2{x - other.x, y - other.y}; }

    inline Vec2 operator*(float s) const { return Vec2{x * s, y * s}; }

    inline Vec2 operator/(float s) const { return Vec2{x / s, y / s}; }

};