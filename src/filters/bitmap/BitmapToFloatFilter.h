#pragma once

#include "../Filter.h"

struct BitmapToFloatFilter : public FilterTyped<Bitmap, FloatImage>
{
    BitmapToFloatFilter()
    {
        m_parameters["threshold"] = FilterParameter{"threshold", 0.0f, 1.0f, 0.5f};
    }

    const char *name() const override { return "Bitmap Distance Field"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    static void distanceTransformChamfer(std::vector<float> &d, size_t w, size_t h, float wOrth, float wDiag)
    {
        const float INF = 1e20f;
        // Forward pass
        for (size_t y = 0; y < h; ++y)
        {
            for (size_t x = 0; x < w; ++x)
            {
                size_t i = y * w + x;
                float v = d[i];
                if (v == 0.0f) continue;
                float best = v;
                if (x > 0) best = std::min(best, d[i - 1] + wOrth);
                if (y > 0) best = std::min(best, d[i - w] + wOrth);
                if (x > 0 && y > 0) best = std::min(best, d[i - 1 - w] + wDiag);
                if (x + 1 < w && y > 0) best = std::min(best, d[i + 1 - w] + wDiag);
                d[i] = best;
            }
        }
        // Backward pass
        for (size_t y = h; y-- > 0; )
        {
            for (size_t x = w; x-- > 0; )
            {
                size_t i = y * w + x;
                float v = d[i];
                float best = v;
                if (x + 1 < w) best = std::min(best, d[i + 1] + wOrth);
                if (y + 1 < h) best = std::min(best, d[i + w] + wOrth);
                if (x + 1 < w && y + 1 < h) best = std::min(best, d[i + 1 + w] + wDiag);
                if (x > 0 && y + 1 < h) best = std::min(best, d[i - 1 + w] + wDiag);
                d[i] = best;
            }
        }
    }

    void applyTyped(const Bitmap &in, FloatImage &out) const override
    {
        const float thresh = m_parameters.at("threshold").value; // 0..1
        const uint8_t cut = (uint8_t)std::round(thresh * 255.0f);
        const size_t w = in.width_px;
        const size_t h = in.height_px;
        const size_t N = w * h;

        out.width_px = w;
        out.height_px = h;
        out.pixel_size_mm = in.pixel_size_mm;
        out.pixels.assign(N, 0.0f);

        if (N == 0)
        {
            out.minValue = 0.0f;
            out.maxValue = 0.0f;
            return;
        }

        // Build binary mask: foreground = pixel >= cut
        std::vector<uint8_t> fg(N, 0);
        bool hasFg = false, hasBg = false;
        for (size_t i = 0; i < N; ++i)
        {
            if (in.pixels[i] >= cut) { fg[i] = 1; hasFg = true; }
            else { fg[i] = 0; hasBg = true; }
        }

        const float wOrth = 1.0f * in.pixel_size_mm;
        const float wDiag = 1.41421356237f * in.pixel_size_mm;
        const float INF = 1e20f;

        // Distance to nearest foreground
        std::vector<float> dToFg(N, INF);
        for (size_t i = 0; i < N; ++i) if (fg[i]) dToFg[i] = 0.0f;
        if (hasFg) distanceTransformChamfer(dToFg, w, h, wOrth, wDiag);

        // Distance to nearest background
        std::vector<float> dToBg(N, INF);
        for (size_t i = 0; i < N; ++i) if (!fg[i]) dToBg[i] = 0.0f;
        if (hasBg) distanceTransformChamfer(dToBg, w, h, wOrth, wDiag);

        // Compose signed distance field (negative inside foreground)
        for (size_t i = 0; i < N; ++i)
        {
            if (fg[i])
                out.pixels[i] = hasBg ? -dToBg[i] : 0.0f;
            else
                out.pixels[i] = hasFg ? dToFg[i] : 0.0f;
        }

        out.computeRange();
    }
};


