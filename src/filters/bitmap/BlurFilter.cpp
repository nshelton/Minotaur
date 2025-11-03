#include "filters/bitmap/BlurFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
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

    const int diam = 2 * radiusPx + 1;
    const int area = diam * diam;

    // Temporary buffer to hold horizontal sliding sums per pixel
    std::vector<uint32_t> hsum(static_cast<size_t>(w) * static_cast<size_t>(h));

    // Horizontal pass: for each row, compute sliding window sum with clamped borders
    for (int y = 0; y < h; ++y)
    {
        const size_t rowOff = static_cast<size_t>(y) * static_cast<size_t>(w);
        // Initial sum for x=0
        int sum = 0;
        for (int kx = -radiusPx; kx <= radiusPx; ++kx)
        {
            const int cx = clampi(kx, 0, w - 1);
            sum += in.pixels[rowOff + static_cast<size_t>(cx)];
        }
        // Slide across row
        for (int x = 0; x < w; ++x)
        {
            hsum[rowOff + static_cast<size_t>(x)] = static_cast<uint32_t>(sum);
            const int addx = clampi(x + radiusPx + 1, 0, w - 1);
            const int remx = clampi(x - radiusPx, 0, w - 1);
            sum += static_cast<int>(in.pixels[rowOff + static_cast<size_t>(addx)]) -
                   static_cast<int>(in.pixels[rowOff + static_cast<size_t>(remx)]);
        }
    }

    // Vertical pass: for each column, compute sliding window sum over horizontal sums
    for (int x = 0; x < w; ++x)
    {
        // Initial sum for y=0
        uint32_t vsum = 0;
        for (int ky = -radiusPx; ky <= radiusPx; ++ky)
        {
            const int cy = clampi(ky, 0, h - 1);
            vsum += hsum[static_cast<size_t>(cy) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        }
        for (int y = 0; y < h; ++y)
        {
            // Average over full 2D box area, with rounding
            const uint32_t total = vsum;
            const uint32_t v = (total + static_cast<uint32_t>(area / 2)) / static_cast<uint32_t>(area);
            out.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = static_cast<uint8_t>(v);

            const int addy = clampi(y + radiusPx + 1, 0, h - 1);
            const int remy = clampi(y - radiusPx, 0, h - 1);
            vsum += hsum[static_cast<size_t>(addy) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            vsum -= hsum[static_cast<size_t>(remy) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        }
    }
}

