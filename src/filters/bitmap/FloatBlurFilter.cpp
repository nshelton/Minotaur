#include "FloatBlurFilter.h"

#include <algorithm>
#include <cmath>
#include <vector>

static inline int clampi_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline void buildGaussianKernelFloat(int radius, std::vector<float> &kernel)
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
        float x = static_cast<float>(i);
        float w = std::exp(-(x * x) / twoSigma2);
        kernel[static_cast<size_t>(i + radius)] = w;
        sum += w;
    }
    if (sum <= 0.0f) sum = 1.0f;
    for (int i = 0; i < size; ++i)
        kernel[static_cast<size_t>(i)] /= sum;
}

void FloatBlurFilter::applyTyped(const FloatImage &in, FloatImage &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    const size_t N = out.width_px * out.height_px;
    out.pixels.resize(N);

    if (in.width_px == 0 || in.height_px == 0 || in.pixels.empty())
    {
        out.pixels.clear();
        out.minValue = 0.0f;
        out.maxValue = 0.0f;
        return;
    }

    const int w = static_cast<int>(in.width_px);
    const int h = static_cast<int>(in.height_px);
    const int radiusPx = std::max(0, static_cast<int>(std::lround(m_parameters.at("radius").value)));
    if (radiusPx == 0)
    {
        out.pixels = in.pixels;
        out.minValue = in.minValue;
        out.maxValue = in.maxValue;
        return;
    }

    std::vector<float> kernel;
    buildGaussianKernelFloat(radiusPx, kernel);

    // Horizontal pass
    std::vector<float> tmp(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    for (int y = 0; y < h; ++y)
    {
        const float *srcRow = in.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        float *dstRow = tmp.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        for (int x = 0; x < w; ++x)
        {
            float sum = 0.0f;
            for (int kx = -radiusPx; kx <= radiusPx; ++kx)
            {
                int cx = clampi_int(x + kx, 0, w - 1);
                float kw = kernel[static_cast<size_t>(kx + radiusPx)];
                sum += srcRow[static_cast<size_t>(cx)] * kw;
            }
            dstRow[static_cast<size_t>(x)] = sum;
        }
    }

    // Vertical pass
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            float sum = 0.0f;
            for (int ky = -radiusPx; ky <= radiusPx; ++ky)
            {
                int cy = clampi_int(y + ky, 0, h - 1);
                float kw = kernel[static_cast<size_t>(ky + radiusPx)];
                sum += tmp[static_cast<size_t>(cy) * static_cast<size_t>(w) + static_cast<size_t>(x)] * kw;
            }
            out.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = sum;
        }
    }

    out.computeRange();
}


