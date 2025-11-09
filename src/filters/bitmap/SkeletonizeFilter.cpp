#include "filters/bitmap/SkeletonizeFilter.h"

#include <vector>
#include <cstdint>
#include <queue>
#include <cmath>
#include <chrono>

namespace
{
    struct IVec2
    {
        int x;
        int y;
    };
    static inline size_t idxOf(int x, int y, int W) { return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x); }

    // Classic Zhang–Suen thinning (binary image: 1 = fg, 0 = bg)
    static void zhangSuenThin(std::vector<uint8_t> &img, int W, int H)
    {
        if (W <= 0 || H <= 0)
            return;

        static const uint8_t LUT[256] = {0, 0, 0, 1, 0, 0, 1, 3, 0, 0, 3, 1, 1, 0,
                                         1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0,
                                         3, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         2, 0, 0, 0, 3, 0, 2, 2, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0,
                                         0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0,
                                         3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0,
                                         2, 0, 0, 0, 3, 1, 0, 0, 1, 3, 0, 0, 0, 0,
                                         0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 1, 3, 1, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 1, 3,
                                         0, 0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         2, 3, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                         0, 0, 3, 3, 0, 1, 0, 0, 0, 0, 2, 2, 0, 0,
                                         2, 0, 0, 0};

        const int ncols = W + 2;
        const int nrows = H + 2;

        auto P = [ncols](int r, int c) -> int
        { return r * ncols + c; };

        std::vector<uint8_t> skeleton(static_cast<size_t>(ncols * nrows), 0);
        std::vector<uint8_t> cleaned(static_cast<size_t>(ncols * nrows), 0);

        // Copy original into 1-pixel padded buffer
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                uint8_t v = img[idxOf(x, y, W)];
                skeleton[P(y + 1, x + 1)] = v;
                cleaned[P(y + 1, x + 1)] = v;
            }
        }

        bool pixel_removed = true;
        while (pixel_removed)
        {
            pixel_removed = false;

            for (int pass = 0; pass < 2; ++pass)
            {
                const bool first_pass = (pass == 0);
                // Start from current skeleton state
                cleaned = skeleton;

                for (int r = 1; r <= H; ++r)
                {
                    for (int c = 1; c <= W; ++c)
                    {
                        if (!skeleton[P(r, c)])
                            continue;

                        // Encode 8-neighborhood into a byte
                        uint8_t code =
                             skeleton[P( r - 1, c - 1)] +
                            2 * skeleton[P( r - 1, c)] +
                            4 * skeleton[P( r - 1, c + 1)] +
                            8 * skeleton[P( r, c + 1)] +
                            16 * skeleton[P( r + 1, c + 1)] +
                            32 * skeleton[P( r + 1, c)] +
                            64 * skeleton[P( r + 1, c - 1)] +
                            128 * skeleton[P( r, c - 1)];

                        uint8_t neighbors = LUT[code];

                        if (neighbors == 0)
                            continue;
                        if (neighbors == 3 || (neighbors == 1 && first_pass) || (neighbors == 2 && !first_pass))
                        {
                            cleaned[P(r, c)] = 0;
                            pixel_removed = true;
                        }
                    }
                }

                // Apply removals of this pass
                skeleton.swap(cleaned);
            }
        }

        // Copy back without padding
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                img[idxOf(x, y, W)] = skeleton[P(y + 1, x + 1)] > 0 ? 0 : 255;
    }
}

void SkeletonizeFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    using clock = std::chrono::high_resolution_clock;
    auto t0 = clock::now();

    const int width = static_cast<int>(in.width_px);
    const int height = static_cast<int>(in.height_px);

    out.width_px = width;
    out.height_px = height;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    if (width <= 0 || height <= 0 || in.pixels.empty())
    {
        out.pixels.clear();
        return;
    }

    for (size_t i = 0; i < out.pixels.size(); ++i)
        out.pixels[i] = in.pixels[i] > 128 ? 0 : 1;

    zhangSuenThin(out.pixels, width, height);
    // assume that the input is binary: 0 = bg, 255 = fg
}
