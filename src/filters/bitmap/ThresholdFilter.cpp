#include "filters/bitmap/ThresholdFilter.h"
#include <cstddef>
#if defined(_MSC_VER)
  #include <intrin.h>
#endif
#include <emmintrin.h> // SSE2

void ThresholdFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(out.width_px) * static_cast<size_t>(out.height_px));

    if (in.width_px <= 0 || in.height_px <= 0 || in.pixels.empty())
    {
        out.pixels.clear();
        return;
    }

    const size_t w = static_cast<size_t>(in.width_px);
    const size_t h = static_cast<size_t>(in.height_px);

    int minV = static_cast<int>(m_parameters.at("min").value);
    int maxV = static_cast<int>(m_parameters.at("max").value);
    if (minV > maxV) std::swap(minV, maxV);
    if (minV < 0) minV = 0; if (minV > 255) minV = 255;
    if (maxV < 0) maxV = 0; if (maxV > 255) maxV = 255;
    const uint8_t minU8 = static_cast<uint8_t>(minV);
    const uint8_t maxU8 = static_cast<uint8_t>(maxV);

    const size_t total = w * h;

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    // SIMD path: process 16 pixels per iteration using unsigned compare via XOR 0x80 and signed compare
    const __m128i minVec = _mm_set1_epi8(static_cast<char>(minU8 ^ 0x80));
    const __m128i maxVec = _mm_set1_epi8(static_cast<char>(maxU8 ^ 0x80));
    size_t i = 0;
    const uint8_t *src = in.pixels.data();
    uint8_t *dst = out.pixels.data();
    for (; i + 16 <= total; i += 16)
    {
        __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
        // Convert to signed domain by XOR with 0x80
        const __m128i signFlip = _mm_set1_epi8(static_cast<char>(0x80));
        __m128i a = _mm_xor_si128(px, signFlip);
        // ge_min = ~(a < min)
        __m128i lt_min = _mm_cmplt_epi8(a, minVec);
        __m128i ge_min = _mm_andnot_si128(lt_min, _mm_set1_epi8(static_cast<char>(0xFF)));
        // le_max = ~(a > max)
        __m128i gt_max = _mm_cmpgt_epi8(a, maxVec);
        __m128i le_max = _mm_andnot_si128(gt_max, _mm_set1_epi8(static_cast<char>(0xFF)));
        // inside = ge_min & le_max
        __m128i inside = _mm_and_si128(ge_min, le_max);
        // outside = ~inside
        __m128i outside = _mm_andnot_si128(inside, _mm_set1_epi8(static_cast<char>(0xFF)));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), outside);
    }
    // tail
    for (; i < total; ++i)
    {
        uint8_t v = src[i];
        bool inside = (v >= minU8) && (v <= maxU8);
        dst[i] = inside ? 0 : 255;
    }
#else
    // Scalar fallback
    for (size_t i = 0; i < total; ++i)
    {
        uint8_t v = in.pixels[i];
        const bool inside = (v >= minU8) && (v <= maxU8);
        out.pixels[i] = inside ? 0 : 255;
    }
#endif
}


