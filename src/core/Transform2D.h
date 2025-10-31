#pragma once

struct Transform2D {
    Vec2 pos{0, 0};
    float scale{1.0f};

    Transform2D(const Vec2& position, float s) : pos(position), scale(s) {}
    Transform2D() : pos{0,0}, scale{1.0f} {}

    Vec2 apply(const Vec2& point) const {
        return point * scale + pos;
    }

    Vec2 applyInverse(const Vec2& point) const {
        return (point - pos) / scale;
    }

    inline Vec2 operator*(const Vec2& point) const {
        return apply(point);
    }

    inline Vec2 operator/(const Vec2& point) const {
        return applyInverse(point);
    }

};
