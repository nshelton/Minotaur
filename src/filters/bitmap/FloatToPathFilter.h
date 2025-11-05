#pragma once

#include "../Filter.h"

struct FloatToPathFilter : public FilterTyped<FloatImage, PathSet>
{
    FloatToPathFilter()
    {
        m_parameters["maximaRadius"] = FilterParameter{"maxima radius (px)", 1.0f, 7.0f, 1.0f};
        m_parameters["connectRadius"] = FilterParameter{"connect radius (px)", 1.0f, 7.0f, 1.0f};
    }

    const char *name() const override { return "Float Maxima to Paths"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const FloatImage &in, PathSet &out) const override
    {
        const size_t w = in.width_px;
        const size_t h = in.height_px;
        out.paths.clear();
        if (w == 0 || h == 0 || in.pixels.empty()) return;

        const float px = in.pixel_size_mm;
        const int rMax = std::max(1, static_cast<int>(std::lround(m_parameters.at("maximaRadius").value)));
        const int rConn = std::max(1, static_cast<int>(std::lround(m_parameters.at("connectRadius").value)));

        // Detect local maxima (8-neighborhood), restrict to positive values
        std::vector<uint8_t> isMax(w * h, 0);
        auto idx = [w](size_t x, size_t y){ return y * w + x; };
        for (size_t y = 0; y < h; ++y)
        {
            for (size_t x = 0; x < w; ++x)
            {
                size_t i = idx(x,y);
                float v = in.pixels[i];
                if (!(v > 0.0f)) continue; // only take ridges in positive (outside) field
                bool maxHere = true;
                for (int dy = -rMax; dy <= rMax && maxHere; ++dy)
                {
                    for (int dx = -rMax; dx <= rMax; ++dx)
                    {
                        if (dx == 0 && dy == 0) continue;
                        int xn = static_cast<int>(x) + dx;
                        int yn = static_cast<int>(y) + dy;
                        if (xn < 0 || yn < 0 || xn >= static_cast<int>(w) || yn >= static_cast<int>(h)) continue;
                        float vn = in.pixels[idx(static_cast<size_t>(xn), static_cast<size_t>(yn))];
                        if (vn > v) { maxHere = false; break; }
                    }
                }
                if (maxHere) isMax[i] = 1;
            }
        }

        // Connect nearby maxima within radius rConn (avoid duplicates by forward half-plane rule)
        for (size_t y = 0; y < h; ++y)
        {
            for (size_t x = 0; x < w; ++x)
            {
                size_t i = idx(x,y);
                if (!isMax[i]) continue;
                for (int dy = -rConn; dy <= rConn; ++dy)
                {
                    for (int dx = -rConn; dx <= rConn; ++dx)
                    {
                        if (dx == 0 && dy == 0) continue;
                        // forward half-plane to avoid duplicates: (dy > 0) or (dy == 0 && dx > 0)
                        if (!(dy > 0 || (dy == 0 && dx > 0))) continue;
                        int xn = static_cast<int>(x) + dx;
                        int yn = static_cast<int>(y) + dy;
                        if (xn < 0 || yn < 0 || xn >= static_cast<int>(w) || yn >= static_cast<int>(h)) continue;
                        size_t j = idx(static_cast<size_t>(xn), static_cast<size_t>(yn));
                        if (!isMax[j]) continue;

                        Path p;
                        p.closed = false;
                        p.points.reserve(2);
                        p.points.push_back(Vec2((static_cast<float>(x) + 0.5f) * px,
                                                (static_cast<float>(y) + 0.5f) * px));
                        p.points.push_back(Vec2((static_cast<float>(xn) + 0.5f) * px,
                                                (static_cast<float>(yn) + 0.5f) * px));
                        out.paths.push_back(std::move(p));
                    }
                }
            }
        }
        out.computeAABB();
    }
};


