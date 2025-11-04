#include "filters/pathset/CurlNoiseFilter.h"

#include <algorithm>
#include <cmath>

// Simple hash to produce deterministic pseudo-random gradients from integer lattice coords
static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t hash3i(int x, int y, int z)
{
    uint32_t ux = static_cast<uint32_t>(x) * 0x8da6b343U; // large primes
    uint32_t uy = static_cast<uint32_t>(y) * 0xd8163841U;
    uint32_t uz = static_cast<uint32_t>(z) * 0xcb1ab31fU;
    return hash32(ux ^ uy ^ uz);
}

static inline float fade(float t)
{
    // Quintic fade for smooth derivatives
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Pick one of 8 gradients based on hash
static inline void grad2(int gx, int gy, int seed, float dx, float dy, float &out)
{
    uint32_t h = hash3i(gx, gy, seed);
    // 8 unit vectors (N, NE, E, SE, S, SW, W, NW)
    static const float g[8][2] = {
        { 1.0f, 0.0f}, { 0.70710678f,  0.70710678f}, { 0.0f, 1.0f},
        {-0.70710678f, 0.70710678f}, {-1.0f, 0.0f}, {-0.70710678f,-0.70710678f},
        { 0.0f,-1.0f}, { 0.70710678f,-0.70710678f}
    };
    const float *v = g[h & 7U];
    out = v[0] * dx + v[1] * dy;
}

static float perlin2(float x, float y, int seed)
{
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);

    float n00, n10, n01, n11;
    grad2(x0, y0, seed, fx,     fy,     n00);
    grad2(x1, y0, seed, fx-1.0f,fy,     n10);
    grad2(x0, y1, seed, fx,     fy-1.0f,n01);
    grad2(x1, y1, seed, fx-1.0f,fy-1.0f,n11);

    float u = fade(fx);
    float v = fade(fy);
    float nx0 = lerpf(n00, n10, u);
    float nx1 = lerpf(n01, n11, u);
    float nxy = lerpf(nx0, nx1, v);
    // Scale to roughly [-1,1]
    return nxy * 1.41421356f; // sqrt(2) to balance gradient magnitude
}

static float fbm2(float x, float y, int octaves, float lacunarity, float gain, int seed)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    // Keep seed advancing per octave to decorrelate
    for (int i = 0; i < octaves; ++i)
    {
        sum += perlin2(x * freq, y * freq, seed + i * 1013) * amp;
        freq *= lacunarity;
        amp *= gain;
    }
    return sum;
}

// Numerical gradient of scalar noise
static inline void noiseGrad2(float x, float y, int octaves, float lacunarity, float gain, int seed, float eps, float &dnx, float &dny)
{
    float e2 = eps * 2.0f;
    float nxp = fbm2(x + eps, y, octaves, lacunarity, gain, seed);
    float nxm = fbm2(x - eps, y, octaves, lacunarity, gain, seed);
    float nyp = fbm2(x, y + eps, octaves, lacunarity, gain, seed);
    float nym = fbm2(x, y - eps, octaves, lacunarity, gain, seed);
    dnx = (nxp - nxm) / e2;
    dny = (nyp - nym) / e2;
}

void CurlNoiseFilter::applyTyped(const PathSet &in, PathSet &out) const
{
    out.color = in.color;
    out.paths.clear();
    out.paths.reserve(in.paths.size());

    const float amplitude = std::max(0.0f, m_parameters.at("amplitudeMm").value);
    float scaleMm = std::max(1.0f, m_parameters.at("scaleMm").value);
    int oct = static_cast<int>(std::round(m_parameters.at("octaves").value));
    if (oct < 1) oct = 1; if (oct > 8) oct = 8;
    const float lac = std::max(1.0f, m_parameters.at("lacunarity").value);
    const float gain = std::clamp(m_parameters.at("gain").value, 0.0f, 1.0f);
    const int seed = static_cast<int>(std::round(m_parameters.at("seed").value));

    // Small step for numerical derivatives in noise space
    const float eps = 0.01f;

    if (amplitude <= 0.0f)
    {
        // Bypass quickly
        out.paths = in.paths;
        out.computeAABB();
        return;
    }

    for (const auto &p : in.paths)
    {
        Path q;
        q.closed = p.closed;
        q.points.reserve(p.points.size());

        for (const Vec2 &pt : p.points)
        {
            // Convert to noise domain by length scale (bigger scale => smoother field)
            const float nx = pt.x / scaleMm;
            const float ny = pt.y / scaleMm;

            float dnx, dny;
            noiseGrad2(nx, ny, oct, lac, gain, seed, eps, dnx, dny);

            // 2D "curl" vector from scalar noise gradient
            // curl(phi) in 2D as a divergence-free field: (dphi/dy, -dphi/dx)
            Vec2 curl(dny, -dnx);

            Vec2 displaced = pt + curl * amplitude;
            q.points.push_back(displaced);
        }

        out.paths.push_back(std::move(q));
    }

    out.computeAABB();
}


