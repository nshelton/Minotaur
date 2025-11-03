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
                          float clipLimitParam,
                          std::array<uint8_t, 256> &outLut)
{
    const int nbins = 256;
    const int nPix = tileW * tileH;
    if (nPix <= 0)
    {
        for (int i = 0; i < nbins; ++i) outLut[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        return;
    }

    std::array<int, 256> hist{};
    hist.fill(0);

    // Histogram
    for (int y = 0; y < tileH; ++y)
    {
        const uint8_t *row = tilePixels + static_cast<size_t>(y) * static_cast<size_t>(tileW);
        for (int x = 0; x < tileW; ++x)
        {
            ++hist[static_cast<size_t>(row[static_cast<size_t>(x)])];
        }
    }

    // Clip limit in counts per bin (scaled by region size)
    if (clipLimitParam > 0.0f)
    {
        int clipLimitCounts = static_cast<int>(clipLimitParam * static_cast<float>(nPix) / static_cast<float>(nbins) + 0.5f);
        if (clipLimitCounts < 1) clipLimitCounts = 1;

        int excess = 0;
        for (int i = 0; i < nbins; ++i)
        {
            if (hist[static_cast<size_t>(i)] > clipLimitCounts)
            {
                excess += hist[static_cast<size_t>(i)] - clipLimitCounts;
                hist[static_cast<size_t>(i)] = clipLimitCounts;
            }
        }
        if (excess > 0)
        {
            const int base = excess / nbins;
            int rem = excess - base * nbins;
            for (int i = 0; i < nbins; ++i)
            {
                hist[static_cast<size_t>(i)] += base;
            }
            // Distribute remainder one by one
            for (int i = 0; i < rem; ++i)
            {
                hist[static_cast<size_t>(i)] += 1;
            }
        }
    }

    // CDF -> LUT
    int cdf = 0;
    int total = 0;
    for (int i = 0; i < nbins; ++i) total += hist[static_cast<size_t>(i)];
    if (total <= 0) total = 1;
    for (int i = 0; i < nbins; ++i)
    {
        cdf += hist[static_cast<size_t>(i)];
        int v = (cdf * 255 + (total / 2)) / total;
        outLut[static_cast<size_t>(i)] = static_cast<uint8_t>(clampi_int(v, 0, 255));
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

    // Build LUTs per tile
    const int numTiles = tilesX * tilesY;
    std::vector<std::array<uint8_t, 256>> luts(static_cast<size_t>(numTiles));
    for (int ty = 0; ty < tilesY; ++ty)
    {
        const int y0 = yCoords[static_cast<size_t>(ty)];
        const int y1 = yCoords[static_cast<size_t>(ty + 1)];
        const int th = std::max(0, y1 - y0);
        for (int tx = 0; tx < tilesX; ++tx)
        {
            const int x0 = xCoords[static_cast<size_t>(tx)];
            const int x1 = xCoords[static_cast<size_t>(tx + 1)];
            const int tw = std::max(0, x1 - x0);

            const uint8_t *tilePtr = in.pixels.data() + static_cast<size_t>(y0) * static_cast<size_t>(w) + static_cast<size_t>(x0);
            // Build a contiguous temporary buffer row by row if width != tw; otherwise we can treat row stride
            // Our builder expects contiguous tile rows; handle per-row inside builder by passing tileW and tileH
            buildClaheLut(tilePtr, tw, th, clipLimit, luts[static_cast<size_t>(ty * tilesX + tx)]);
        }
    }

    // Apply with bilinear interpolation between neighboring tile LUTs
    for (int y = 0; y < h; ++y)
    {
        const int pty = clampi_int((y * tilesY) / h, 0, tilesY - 1);
        const int pty1 = std::min(pty + 1, tilesY - 1);
        const int y0 = yCoords[static_cast<size_t>(pty)];
        const int y1 = yCoords[static_cast<size_t>(pty + 1)];
        const int dy = std::max(1, y1 - y0);
        const float fy = static_cast<float>(y - y0) / static_cast<float>(dy);

        const uint8_t *srcRow = in.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        uint8_t *dstRow = out.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w);

        for (int x = 0; x < w; ++x)
        {
            const int ptx = clampi_int((x * tilesX) / w, 0, tilesX - 1);
            const int ptx1 = std::min(ptx + 1, tilesX - 1);

            const int x0 = xCoords[static_cast<size_t>(ptx)];
            const int x1 = xCoords[static_cast<size_t>(ptx + 1)];
            const int dx = std::max(1, x1 - x0);
            const float fx = static_cast<float>(x - x0) / static_cast<float>(dx);

            const uint8_t v = srcRow[static_cast<size_t>(x)];

            const std::array<uint8_t, 256> &L00 = luts[static_cast<size_t>(pty * tilesX + ptx)];
            const std::array<uint8_t, 256> &L10 = luts[static_cast<size_t>(pty * tilesX + ptx1)];
            const std::array<uint8_t, 256> &L01 = luts[static_cast<size_t>(pty1 * tilesX + ptx)];
            const std::array<uint8_t, 256> &L11 = luts[static_cast<size_t>(pty1 * tilesX + ptx1)];

            const float v00 = static_cast<float>(L00[static_cast<size_t>(v)]);
            const float v10 = static_cast<float>(L10[static_cast<size_t>(v)]);
            const float v01 = static_cast<float>(L01[static_cast<size_t>(v)]);
            const float v11 = static_cast<float>(L11[static_cast<size_t>(v)]);

            const float v0 = v00 + (v10 - v00) * fx;
            const float v1 = v01 + (v11 - v01) * fx;
            const int vo = static_cast<int>(std::lround(v0 + (v1 - v0) * fy));
            dstRow[static_cast<size_t>(x)] = static_cast<uint8_t>(clampi_int(vo, 0, 255));
        }
    }
}


