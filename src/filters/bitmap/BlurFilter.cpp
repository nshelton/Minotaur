#include "filters/bitmap/BlurFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#if defined(_MSC_VER)
  #include <intrin.h>
#endif
#include <emmintrin.h> // SSE2

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// Build a separable 1D Gaussian kernel with integer weights scaled by 1024
static inline void buildGaussianKernel(int radius, std::vector<int16_t> &kernel, int &weightSum)
{
    const int size = 2 * radius + 1;
    kernel.resize(static_cast<size_t>(size));

    // Derive sigma from radius (radius ~ 3*sigma). Guard against zero
    const float sigma = std::max(0.5f, static_cast<float>(radius) / 3.0f);
    const float twoSigma2 = 2.0f * sigma * sigma;

    std::vector<float> f(size);
    float sumf = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        const float x = static_cast<float>(i);
        const float w = std::exp(-(x * x) / twoSigma2);
        f[static_cast<size_t>(i + radius)] = w;
        sumf += w;
    }

    // Scale float weights to 16-bit integers
    const float scale = 1024.0f; // power-of-two friendly
    weightSum = 0;
    for (int i = 0; i < size; ++i)
    {
        int v = static_cast<int>(std::lround((f[static_cast<size_t>(i)] / sumf) * scale));
        if (v <= 0) v = 1; // ensure non-zero contribution to keep normalization stable
        if (v > 32767) v = 32767;
        kernel[static_cast<size_t>(i)] = static_cast<int16_t>(v);
        weightSum += v;
    }

    // Normalize so that integer sum equals scale as closely as possible
    if (weightSum == 0)
    {
        // Fallback: delta kernel
        std::fill(kernel.begin(), kernel.end(), 0);
        kernel[static_cast<size_t>(radius)] = static_cast<int16_t>(static_cast<int>(scale));
        weightSum = static_cast<int>(scale);
    }
}

void BlurFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(out.width_px) * static_cast<size_t>(out.height_px));

    // Validate input
    if (in.width_px == 0 || in.height_px == 0 || in.pixels.empty())
    {
        out.pixels.clear();
        return;
    }

    const int w = static_cast<int>(in.width_px);
    const int h = static_cast<int>(in.height_px);

    // Radius in pixels as integer (rounded), clamp to non-negative
    const int radiusPx = std::max(0, static_cast<int>(std::lround(m_parameters.at("radius").value)));
    if (radiusPx == 0)
    {
        out.pixels = in.pixels;
        return;
    }

    // Build Gaussian kernel (separable)
    std::vector<int16_t> kernel;
    int weightSum = 0;
    buildGaussianKernel(radiusPx, kernel, weightSum);

    // Temporary buffer for horizontal pass
    std::vector<uint8_t> tmp(static_cast<size_t>(w) * static_cast<size_t>(h));

    // Horizontal pass: SIMD for the interior; scalar for borders and tails
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *srcRow = in.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        uint8_t *dstRow = tmp.data() + static_cast<size_t>(y) * static_cast<size_t>(w);

        // Left border (clamped)
        for (int x = 0; x < std::min(radiusPx, w); ++x)
        {
            int sum = 0;
            for (int kx = -radiusPx; kx <= radiusPx; ++kx)
            {
                const int cx = clampi(x + kx, 0, w - 1);
                const int kw = static_cast<int>(kernel[static_cast<size_t>(kx + radiusPx)]);
                sum += static_cast<int>(srcRow[static_cast<size_t>(cx)]) * kw;
            }
            const int v = (sum + (weightSum / 2)) / weightSum;
            dstRow[static_cast<size_t>(x)] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }

        // SIMD interior: process 8 pixels per iteration
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        const int xStart = radiusPx;
        const int xEndSimd = std::max(xStart, w - radiusPx);
        const __m128 invSum = _mm_set1_ps(1.0f / static_cast<float>(weightSum));
        const __m128 half = _mm_set1_ps(0.5f);
        const __m128i zero = _mm_setzero_si128();
        int x = xStart;
        for (; x + 8 <= xEndSimd; x += 8)
        {
            __m128i accLo32 = _mm_setzero_si128(); // 4 lanes
            __m128i accHi32 = _mm_setzero_si128(); // 4 lanes

            for (int kx = -radiusPx; kx <= radiusPx; ++kx)
            {
                const int kw = static_cast<int>(kernel[static_cast<size_t>(kx + radiusPx)]);
                const __m128i kw16 = _mm_set1_epi16(static_cast<short>(kw));

                const uint8_t *p = srcRow + static_cast<size_t>(x + kx);
                const __m128i px8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(p)); // 8 bytes
                const __m128i px16 = _mm_unpacklo_epi8(px8, zero); // 8 x u16
                const __m128i mul16 = _mm_mullo_epi16(px16, kw16); // 8 x u16 (may wrap but widened next)

                const __m128i lo32 = _mm_unpacklo_epi16(mul16, zero);
                const __m128i hi32 = _mm_unpackhi_epi16(mul16, zero);
                accLo32 = _mm_add_epi32(accLo32, lo32);
                accHi32 = _mm_add_epi32(accHi32, hi32);
            }

            // Convert to float, normalize, round to nearest int
            __m128 fLo = _mm_cvtepi32_ps(accLo32);
            __m128 fHi = _mm_cvtepi32_ps(accHi32);
            fLo = _mm_add_ps(_mm_mul_ps(fLo, invSum), half);
            fHi = _mm_add_ps(_mm_mul_ps(fHi, invSum), half);
            const __m128i iLo = _mm_cvtps_epi32(fLo);
            const __m128i iHi = _mm_cvtps_epi32(fHi);
            const __m128i pack16 = _mm_packs_epi32(iLo, iHi);
            const __m128i pack8 = _mm_packus_epi16(pack16, zero);
            _mm_storel_epi64(reinterpret_cast<__m128i *>(dstRow + static_cast<size_t>(x)), pack8);
        }
#else
        int x = radiusPx;
#endif

        // Right interior/tail including the clamped right border
        for (; x < w; ++x)
        {
            int sum = 0;
            for (int kx = -radiusPx; kx <= radiusPx; ++kx)
            {
                const int cx = clampi(x + kx, 0, w - 1);
                const int kw = static_cast<int>(kernel[static_cast<size_t>(kx + radiusPx)]);
                sum += static_cast<int>(srcRow[static_cast<size_t>(cx)]) * kw;
            }
            const int v = (sum + (weightSum / 2)) / weightSum;
            dstRow[static_cast<size_t>(x)] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    }

    // Vertical pass (scalar)
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int sum = 0;
            for (int ky = -radiusPx; ky <= radiusPx; ++ky)
            {
                const int cy = clampi(y + ky, 0, h - 1);
                const int kw = static_cast<int>(kernel[static_cast<size_t>(ky + radiusPx)]);
                sum += static_cast<int>(tmp[static_cast<size_t>(cy) * static_cast<size_t>(w) + static_cast<size_t>(x)]) * kw;
            }
            const int v = (sum + (weightSum / 2)) / weightSum;
            out.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    }
}

