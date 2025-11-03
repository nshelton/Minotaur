#include "filters/bitmap/CannyFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
    static inline int clampi(int v, int lo, int hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }

    // Build a simple separable Gaussian kernel for a given integer radius.
    static inline void buildGaussianKernel(int radius, std::vector<float> &kernel)
    {
        const int size = 2 * radius + 1;
        kernel.resize(static_cast<size_t>(size));
        if (radius <= 0)
        {
            kernel[0] = 1.0f;
            return;
        }

        const float sigma = std::max(0.5f, static_cast<float>(radius) / 3.0f);
        const float twoSigma2 = 2.0f * sigma * sigma;

        float sum = 0.0f;
        for (int i = -radius; i <= radius; ++i)
        {
            const float x = static_cast<float>(i);
            const float w = std::exp(-(x * x) / twoSigma2);
            kernel[static_cast<size_t>(i + radius)] = w;
            sum += w;
        }
        for (int i = 0; i < size; ++i)
        {
            kernel[static_cast<size_t>(i)] /= sum;
        }
    }
}

void CannyFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(out.width_px) * static_cast<size_t>(out.height_px));

    if (in.width_px == 0 || in.height_px == 0 || in.pixels.empty())
    {
        out.pixels.clear();
        return;
    }

    const int w = static_cast<int>(in.width_px);
    const int h = static_cast<int>(in.height_px);

    const int blurRadius = clampi(static_cast<int>(std::lround(m_parameters.at("blur_radius_px").value)), 0, 64);
    int lowTh = clampi(static_cast<int>(std::lround(m_parameters.at("low_threshold").value)), 0, 255);
    int highTh = clampi(static_cast<int>(std::lround(m_parameters.at("high_threshold").value)), 0, 255);
    if (lowTh > highTh) std::swap(lowTh, highTh);

    // 1) Optional Gaussian blur (separable)
    std::vector<uint8_t> blurred(static_cast<size_t>(w) * static_cast<size_t>(h));
    if (blurRadius > 0)
    {
        std::vector<float> kernel;
        buildGaussianKernel(blurRadius, kernel);
        const int size = 2 * blurRadius + 1;

        // Horizontal pass (float accumulator, then clamp to 0..255)
        std::vector<uint8_t> tmp(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src = in.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
            uint8_t *dst = tmp.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
            for (int x = 0; x < w; ++x)
            {
                float sum = 0.0f;
                for (int k = -blurRadius; k <= blurRadius; ++k)
                {
                    const int cx = clampi(x + k, 0, w - 1);
                    sum += static_cast<float>(src[static_cast<size_t>(cx)]) * kernel[static_cast<size_t>(k + blurRadius)];
                }
                int v = static_cast<int>(std::lround(sum));
                dst[static_cast<size_t>(x)] = static_cast<uint8_t>(std::clamp(v, 0, 255));
            }
        }

        // Vertical pass
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float sum = 0.0f;
                for (int k = -blurRadius; k <= blurRadius; ++k)
                {
                    const int cy = clampi(y + k, 0, h - 1);
                    sum += static_cast<float>(tmp[static_cast<size_t>(cy) * static_cast<size_t>(w) + static_cast<size_t>(x)]) * kernel[static_cast<size_t>(k + blurRadius)];
                }
                int v = static_cast<int>(std::lround(sum));
                blurred[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = static_cast<uint8_t>(std::clamp(v, 0, 255));
            }
        }
    }
    else
    {
        blurred = in.pixels;
    }

    // 2) Sobel gradients (3x3), compute L1 magnitude and orientation
    std::vector<uint8_t> gradMag(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    std::vector<uint8_t> dirBin(static_cast<size_t>(w) * static_cast<size_t>(h), 0); // 0,1,2,3 for 0,45,90,135

    for (int y = 1; y < h - 1; ++y)
    {
        for (int x = 1; x < w - 1; ++x)
        {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const int tl = blurred[static_cast<size_t>(y - 1) * static_cast<size_t>(w) + static_cast<size_t>(x - 1)];
            const int tc = blurred[static_cast<size_t>(y - 1) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            const int tr = blurred[static_cast<size_t>(y - 1) * static_cast<size_t>(w) + static_cast<size_t>(x + 1)];
            const int ml = blurred[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x - 1)];
            const int mr = blurred[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x + 1)];
            const int bl = blurred[static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x - 1)];
            const int bc = blurred[static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            const int br = blurred[static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x + 1)];

            const int gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
            const int gy = -tl - 2 * tc - tr + bl + 2 * bc + br;

            const int mag = std::clamp(std::abs(gx) + std::abs(gy), 0, 255); // L1 magnitude, clamped to 0..255
            gradMag[i] = static_cast<uint8_t>(mag);

            float angle = std::atan2(static_cast<float>(gy), static_cast<float>(gx)) * 57.2957795f; // rad->deg
            if (angle < 0.0f) angle += 180.0f;
            uint8_t bin;
            if (angle < 22.5f || angle >= 157.5f) bin = 0;         // 0 deg
            else if (angle < 67.5f) bin = 1;                        // 45 deg
            else if (angle < 112.5f) bin = 2;                       // 90 deg
            else bin = 3;                                           // 135 deg
            dirBin[i] = bin;
        }
    }

    // 3) Non-maximum suppression
    std::vector<uint8_t> nms(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (int y = 1; y < h - 1; ++y)
    {
        for (int x = 1; x < w - 1; ++x)
        {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const uint8_t m = gradMag[i];
            const uint8_t d = dirBin[i];

            uint8_t m1 = 0, m2 = 0;
            switch (d)
            {
                case 0: // 0 deg: left/right
                    m1 = gradMag[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x - 1)];
                    m2 = gradMag[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x + 1)];
                    break;
                case 1: // 45 deg: diag TL-BR
                    m1 = gradMag[static_cast<size_t>(y - 1) * static_cast<size_t>(w) + static_cast<size_t>(x - 1)];
                    m2 = gradMag[static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x + 1)];
                    break;
                case 2: // 90 deg: up/down
                    m1 = gradMag[static_cast<size_t>(y - 1) * static_cast<size_t>(w) + static_cast<size_t>(x)];
                    m2 = gradMag[static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x)];
                    break;
                default: // 135 deg: diag BL-TR
                    m1 = gradMag[static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x - 1)];
                    m2 = gradMag[static_cast<size_t>(y - 1) * static_cast<size_t>(w) + static_cast<size_t>(x + 1)];
                    break;
            }

            if (m >= m1 && m >= m2)
                nms[i] = m;
            else
                nms[i] = 0;
        }
    }

    // 4) Double threshold
    constexpr uint8_t STRONG = 255;
    constexpr uint8_t WEAK = 128;
    std::vector<uint8_t> edges(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (int y = 1; y < h - 1; ++y)
    {
        for (int x = 1; x < w - 1; ++x)
        {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const int v = static_cast<int>(nms[i]);
            if (v >= highTh) edges[i] = STRONG;
            else if (v >= lowTh) edges[i] = WEAK;
            else edges[i] = 0;
        }
    }

    // 5) Hysteresis (8-connected)
    std::vector<uint8_t> outMask = edges; // work buffer
    std::vector<int> stack;
    stack.reserve(1024);
    auto idx = [w](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); };

    for (int y = 1; y < h - 1; ++y)
    {
        for (int x = 1; x < w - 1; ++x)
        {
            if (outMask[idx(x, y)] == STRONG)
            {
                stack.clear();
                stack.push_back(y * w + x);
                while (!stack.empty())
                {
                    int p = stack.back();
                    stack.pop_back();
                    const int px = p % w;
                    const int py = p / w;
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0) continue;
                            const int nx = px + dx;
                            const int ny = py + dy;
                            if (nx <= 0 || nx >= w - 1 || ny <= 0 || ny >= h - 1) continue;
                            size_t ni = idx(nx, ny);
                            if (outMask[ni] == WEAK)
                            {
                                outMask[ni] = STRONG;
                                stack.push_back(ny * w + nx);
                            }
                        }
                    }
                }
            }
        }
    }

    // Suppress remaining weak pixels
    for (size_t i = 0; i < outMask.size(); ++i)
    {
        out.pixels[i] = (outMask[i] == STRONG) ? 255 : 0;
    }
}


