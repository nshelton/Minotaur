#include "filters/bitmap/ErodeDilateFilter.h"

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath> // added

namespace
{
    struct IVec2
    {
        int x;
        int y;
        IVec2(int _x, int _y) : x(_x), y(_y) {}
    };
    static inline size_t idxOf(int x, int y, int W) { return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x); }

    static inline uint8_t clampAt(const std::vector<uint8_t> &pix, int x, int y, int W, int H)
    {
        x = std::min(std::max(x, 0), W - 1);
        y = std::min(std::max(y, 0), H - 1);
        return pix[idxOf(x, y, W)];
    }
}

void ErodeDilateFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    const int W = static_cast<int>(in.width_px);
    const int H = static_cast<int>(in.height_px);
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.assign(static_cast<size_t>(W * H), 255);

    if (W <= 0 || H <= 0 || in.pixels.empty())
        return;

    const int op = static_cast<int>(std::round(std::clamp(m_parameters.at("operation").value, 0.0f, 1.0f))); // 0=erode,1=dilate
    const int iterations = static_cast<int>(std::round(std::clamp(m_parameters.at("iterations").value, 1.0f, 8.0f)));
    const int kSize = 3; // fixed 3x3 kernel for now
    const int radius = 1;
    const uint8_t thresh = 128;

    // Precompute kernel offsets (neighbors), center handled separately.

    // Binarize input: foreground=255 if >= threshold, else 0
    std::vector<uint8_t> cur(static_cast<size_t>(W * H), 0);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const uint8_t v = in.pixels[idxOf(x, y, W)];
            cur[idxOf(x, y, W)] = (v >= thresh) ? 255u : 0u;
        }
    }

    size_t counter = 0;

    //morphology
    // IVec2 kernelOffsets[8] = {
    //     IVec2(-1, -1), IVec2(0, -1), IVec2(1, -1),
    //     IVec2(-1, 0),                  IVec2(1, 0),
    //     IVec2(-1, 1), IVec2(0, 1), IVec2(1, 1)
    // };

    const int kernelSize = 4;
    IVec2 kernelOffsets[kernelSize] = {
        IVec2(0, -1), 
        IVec2(-1, 0), IVec2(1, 0),
        IVec2(0, 1)
    };

    // Perform morphology
    for (int it = 0; it < iterations; ++it)
    {
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                counter = 0;
                for (int i = 0; i < kernelSize; ++i)
                {
                    const int kx = kernelOffsets[i].x;
                    const int ky = kernelOffsets[i].y;
                    const uint8_t nv = clampAt(cur, x + kx, y + ky, W, H);
                        if (nv == 0)
                        {
                            counter++;
                        }
                }

                if (op == 0) // erode
                {
                    // if any neighbor is background, set to background
                    if (counter > 0)
                        out.pixels[idxOf(x, y, W)] = 0;
                    else
                        out.pixels[idxOf(x, y, W)] = 255;
                }
                else // dilate
                {
                    // if any neighbor is foreground, set to foreground
                    if (counter < kernelSize)
                        out.pixels[idxOf(x, y, W)] = 255;
                    else
                        out.pixels[idxOf(x, y, W)] = 0;
                }
            }
        }
        // Swap buffers for next iteration
        if (it < iterations - 1)
        {
            cur.swap(out.pixels);
        }
    }
}