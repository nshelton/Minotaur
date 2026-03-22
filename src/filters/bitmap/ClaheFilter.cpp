#include "filters/bitmap/ClaheFilter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

static inline int clampi_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// Build per-tile LUT (256 entries) with optional clipping and redistribution
static void buildClaheLut(const uint8_t *tilePixels,
                          int tileW,
                          int tileH,
                          int stride,
                          float clipLimitParam,
                          std::array<uint8_t, 256> &outLut)
{
    const int nbins = 256;
    const int nPix = tileW * tileH;
    if (nPix <= 0)
    {
        for (int i = 0; i < nbins; ++i) outLut[i] = static_cast<uint8_t>(i);
        return;
    }

    std::array<int, 256> hist{};
    hist.fill(0);

    // Histogram — use stride (full image width) to step between rows
    for (int y = 0; y < tileH; ++y)
    {
        const uint8_t *row = tilePixels + y * stride;
        for (int x = 0; x < tileW; ++x)
            ++hist[row[x]];
    }

    // Clip limit in counts per bin (scaled by region size)
    if (clipLimitParam > 0.0f)
    {
        int clipLimitCounts = static_cast<int>(clipLimitParam * static_cast<float>(nPix) / static_cast<float>(nbins) + 0.5f);
        if (clipLimitCounts < 1) clipLimitCounts = 1;

        int excess = 0;
        for (int i = 0; i < nbins; ++i)
        {
            if (hist[i] > clipLimitCounts)
            {
                excess += hist[i] - clipLimitCounts;
                hist[i] = clipLimitCounts;
            }
        }
        if (excess > 0)
        {
            const int base = excess / nbins;
            int rem = excess - base * nbins;
            for (int i = 0; i < nbins; ++i)
                hist[i] += base;
            // Distribute remainder with rotating offset to avoid dark-bin bias
            const int step = std::max(1, nbins / rem);
            for (int i = 0, idx = 0; i < rem; ++i, idx = (idx + step) % nbins)
                hist[idx] += 1;
        }
    }

    // CDF -> LUT
    int cdf = 0;
    int total = 0;
    for (int i = 0; i < nbins; ++i) total += hist[i];
    if (total <= 0) total = 1;
    for (int i = 0; i < nbins; ++i)
    {
        cdf += hist[i];
        int v = (cdf * 255 + (total / 2)) / total;
        outLut[i] = static_cast<uint8_t>(clampi_int(v, 0, 255));
    }
}

void ClaheFilter::applyTyped(const Bitmap &in, Bitmap &out) const
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

    const int tilesX = clampi_int(static_cast<int>(std::lround(m_parameters.at("tilesX").value)), 1, 64);
    const int tilesY = clampi_int(static_cast<int>(std::lround(m_parameters.at("tilesY").value)), 1, 64);
    const float clipLimit = m_parameters.at("clipLimit").value;

    // Precompute tile boundaries using proportional splits to cover the image exactly
    std::vector<int> xCoords(static_cast<size_t>(tilesX + 1));
    std::vector<int> yCoords(static_cast<size_t>(tilesY + 1));
    for (int i = 0; i <= tilesX; ++i)
        xCoords[static_cast<size_t>(i)] = (i * w) / tilesX;
    for (int j = 0; j <= tilesY; ++j)
        yCoords[static_cast<size_t>(j)] = (j * h) / tilesY;

    // Compute tile centers for interpolation
    std::vector<float> tileCenterX(static_cast<size_t>(tilesX));
    std::vector<float> tileCenterY(static_cast<size_t>(tilesY));
    for (int i = 0; i < tilesX; ++i)
        tileCenterX[i] = 0.5f * static_cast<float>(xCoords[i] + xCoords[i + 1]);
    for (int j = 0; j < tilesY; ++j)
        tileCenterY[j] = 0.5f * static_cast<float>(yCoords[j] + yCoords[j + 1]);

    // Build LUTs per tile
    const int numTiles = tilesX * tilesY;
    std::vector<std::array<uint8_t, 256>> luts(static_cast<size_t>(numTiles));
    for (int ty = 0; ty < tilesY; ++ty)
    {
        const int y0 = yCoords[ty];
        const int y1 = yCoords[ty + 1];
        const int th = std::max(0, y1 - y0);
        for (int tx = 0; tx < tilesX; ++tx)
        {
            const int x0 = xCoords[tx];
            const int x1 = xCoords[tx + 1];
            const int tw = std::max(0, x1 - x0);

            const uint8_t *tilePtr = in.pixels.data() + y0 * w + x0;
            buildClaheLut(tilePtr, tw, th, w, clipLimit, luts[ty * tilesX + tx]);
        }
    }

    // Apply with bilinear interpolation between neighboring tile centers
    for (int y = 0; y < h; ++y)
    {
        const float py = static_cast<float>(y);

        // Find the two tile centers that bracket this y coordinate
        int ty0 = tilesY - 2;
        for (int j = 0; j < tilesY - 1; ++j)
        {
            if (py < tileCenterY[j + 1]) { ty0 = j; break; }
        }
        const int ty1 = std::min(ty0 + 1, tilesY - 1);
        const float cyDen = tileCenterY[ty1] - tileCenterY[ty0];
        const float fy = (cyDen > 0.0f) ? std::clamp((py - tileCenterY[ty0]) / cyDen, 0.0f, 1.0f) : 0.0f;

        const uint8_t *srcRow = in.pixels.data() + y * w;
        uint8_t *dstRow = out.pixels.data() + y * w;

        for (int x = 0; x < w; ++x)
        {
            const float px = static_cast<float>(x);

            // Find the two tile centers that bracket this x coordinate
            int tx0 = tilesX - 2;
            for (int i = 0; i < tilesX - 1; ++i)
            {
                if (px < tileCenterX[i + 1]) { tx0 = i; break; }
            }
            const int tx1 = std::min(tx0 + 1, tilesX - 1);
            const float cxDen = tileCenterX[tx1] - tileCenterX[tx0];
            const float fx = (cxDen > 0.0f) ? std::clamp((px - tileCenterX[tx0]) / cxDen, 0.0f, 1.0f) : 0.0f;

            const uint8_t v = srcRow[x];

            const auto &L00 = luts[ty0 * tilesX + tx0];
            const auto &L10 = luts[ty0 * tilesX + tx1];
            const auto &L01 = luts[ty1 * tilesX + tx0];
            const auto &L11 = luts[ty1 * tilesX + tx1];

            const float v00 = static_cast<float>(L00[v]);
            const float v10 = static_cast<float>(L10[v]);
            const float v01 = static_cast<float>(L01[v]);
            const float v11 = static_cast<float>(L11[v]);

            const float vi0 = v00 + (v10 - v00) * fx;
            const float vi1 = v01 + (v11 - v01) * fx;
            const int vo = static_cast<int>(std::lround(vi0 + (vi1 - vi0) * fy));
            dstRow[x] = static_cast<uint8_t>(clampi_int(vo, 0, 255));
        }
    }
}


