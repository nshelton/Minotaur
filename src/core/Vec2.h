#pragma once

struct Vec2
{
    float x;
    float y;

    Vec2(float x, float y) : x(x), y(y) {}
    Vec2(int x, int y) : x(static_cast<float>(x)), y(static_cast<float>(y)) {}
    Vec2()
    {
        x = 0.0f;
        y = 0.0f;
    }

    inline Vec2 operator+(const Vec2 &other) const { return Vec2{x + other.x, y + other.y}; }

    inline Vec2 operator-(const Vec2 &other) const { return Vec2{x - other.x, y - other.y}; }

    inline Vec2 operator*(float s) const { return Vec2{x * s, y * s}; }

    inline Vec2 operator/(float s) const { return Vec2{x / s, y / s}; }

    inline Vec2 &operator+=(const Vec2 &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }
    inline Vec2 &operator-=(const Vec2 &other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    inline Vec2 &operator*=(float s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    inline Vec2 &operator/=(float s)
    {
        x /= s;
        y /= s;
        return *this;
    }
    inline Vec2 operator*(const Vec2 &other) const { return Vec2{x * other.x, y * other.y}; }
    inline Vec2 operator/(const Vec2 &other) const { return Vec2{x / other.x, y / other.y}; }
    inline Vec2 operator-() const { return Vec2{-x, -y}; }

    inline Vec2 operator+(const float s) const { return Vec2{x + s, y + s}; }
    inline Vec2 operator-(const float s) const { return Vec2{x - s, y - s}; }
};