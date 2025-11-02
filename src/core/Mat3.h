#pragma once

#include <memory>
#include "core/Vec2.h"

struct Mat3
{

    // Column-major (OpenGL-style) 3x3 matrix
    float m[9];

    // Default constructor: identity
    Mat3()
    {
        m[0] = 1;
        m[3] = 0;
        m[6] = 0;
        m[1] = 0;
        m[4] = 1;
        m[7] = 0;
        m[2] = 0;
        m[5] = 0;
        m[8] = 1;
    }

    // Copy constructor
    Mat3(const Mat3 &other)
    {
        std::memcpy(m, other.m, sizeof(m));
    }

    // Assignment
    Mat3 &operator=(const Mat3 &other)
    {
        if (this != &other)
            std::memcpy(m, other.m, sizeof(m));
        return *this;
    }

    static Mat3 identity() { return Mat3(); }

    static Mat3 translation(float tx, float ty)
    {
        Mat3 r = identity();
        r.m[6] = tx;
        r.m[7] = ty;
        return r;
    }

    static Mat3 translation(Vec2 t)
    {
        Mat3 r = identity();
        r.m[6] = t.x;
        r.m[7] = t.y;
        return r;
    }

    static Mat3 scale(Vec2 s)
    {
        Mat3 r = identity();
        r.m[0] = s.x;
        r.m[4] = s.y;
        return r;
    }

    static Mat3 scale(float sx, float sy)
    {
        Mat3 r = identity();
        r.m[0] = sx;
        r.m[4] = sy;
        return r;
    }

    void setTranslation(Vec2 t)
    {
        m[6] = t.x;
        m[7] = t.y;
    }

    void setScale(Vec2 s)
    {
        m[0] = s.x;
        m[4] = s.y;
    }

    Vec2 translation() const
    {
        return Vec2{m[6], m[7]};
    }

    void setOrtho(float left, float right, float bottom, float top)
    {
        m[0] = 2.0f / (right - left);
        m[3] = 0.0f;
        m[6] = -(right + left) / (right - left);
        m[1] = 0.0f;
        m[4] = 2.0f / (top - bottom);
        m[7] = -(top + bottom) / (top - bottom);
        m[2] = 0.0f;
        m[5] = 0.0f;
        m[8] = 1.0f;
    }

    void translate(const Vec2 &t)
    {
        m[6] += t.x;
        m[7] += t.y;
    }

    Vec2 scale() const { return Vec2(m[0], m[4]); } // assuming uniform scale

    Vec2 transformPoint(const Vec2 &v) const
    {
        return {
            m[0] * v.x + m[3] * v.y + m[6],
            m[1] * v.x + m[4] * v.y + m[7]};
    }

    Mat3 operator*(const Mat3 &rhs) const
    {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int rIdx = 0; rIdx < 3; ++rIdx)
                r.m[c * 3 + rIdx] =
                    m[0 * 3 + rIdx] * rhs.m[c * 3 + 0] +
                    m[1 * 3 + rIdx] * rhs.m[c * 3 + 1] +
                    m[2 * 3 + rIdx] * rhs.m[c * 3 + 2];
        return r;
    }

    Mat3 operator*=(const Mat3 &rhs)
    {
        *this = *this * rhs;
        return *this;
    }

    Mat3(const Vec2 &position, float s)
    {
        m[0] = s;
        m[3] = 0;
        m[6] = position.x;
        m[1] = 0;
        m[4] = s;
        m[7] = position.y;
        m[2] = 0;
        m[5] = 0;
        m[8] = 1;
    }

    // Apply to a 2D point (affine transform)
    Vec2 apply(const Vec2 &p) const
    {
        return {
            m[0] * p.x + m[3] * p.y + m[6],
            m[1] * p.x + m[4] * p.y + m[7]};
    }

    // Apply inverse of this affine transform to a point
    Vec2 applyInverse(const Vec2 &p) const
    {
        // top-left 2x2
        float a = m[0], c = m[3];
        float b = m[1], d = m[4];
        float tx = m[6], ty = m[7];

        // remove translation
        float px = p.x - tx;
        float py = p.y - ty;

        // inverse of 2x2
        float det = a * d - b * c;

        float invDet = 1.0f / det;

        // [a c; b d]^{-1} = (1/det) * [ d -c; -b a ]
        return {
            (d * px - c * py) * invDet,
            (-b * px + a * py) * invDet};
    }

    inline Vec2 operator*(const Vec2 &point) const
    {
        return apply(point);
    }

    inline Vec2 operator/(const Vec2 &point) const
    {
        return applyInverse(point);
    }
};
