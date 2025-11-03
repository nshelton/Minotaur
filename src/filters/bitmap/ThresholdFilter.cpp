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
    const uint8_t th = static_cast<uint8_t>(m_parameters.at("threshold").value);

    const size_t total = w * h;

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    // SIMD path: process 16 pixels per iteration using unsigned compare via XOR 0x80 and signed compare
    const __m128i thVec = _mm_set1_epi8(static_cast<char>(th ^ 0x80));
    size_t i = 0;
    const uint8_t *src = in.pixels.data();
    uint8_t *dst = out.pixels.data();
    for (; i + 16 <= total; i += 16)
    {
        __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
        // Convert to signed domain by XOR with 0x80
        const __m128i signFlip = _mm_set1_epi8(static_cast<char>(0x80));
        __m128i a = _mm_xor_si128(px, signFlip);
        // a < th ? 0xFF : 0x00
        __m128i lt = _mm_cmplt_epi8(a, thVec);
        // ge = ~lt --> 0xFF where a >= th
        __m128i ge = _mm_andnot_si128(lt, _mm_set1_epi8(static_cast<char>(0xFF)));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), ge);
    }
    // tail
    for (; i < total; ++i)
    {
        dst[i] = (src[i] >= th) ? 255 : 0;
    }
#else
    // Scalar fallback
    for (size_t i = 0; i < total; ++i)
    {
        uint8_t v = in.pixels[i];
        out.pixels[i] = (v >= th) ? 255 : 0;
    }
#endif
}


