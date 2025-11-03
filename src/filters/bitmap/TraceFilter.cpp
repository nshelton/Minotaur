#include "filters/bitmap/TraceFilter.h"

void TraceFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
    out.paths.clear();
    out.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

    if (in.width_px <= 0 || in.height_px <= 0 || in.pixels.empty())
    {
        out.computeAABB();
        return;
    }

    // Create a polyline per row connecting pixels above threshold
    for (int y = 0; y < in.height_px; ++y)
    {
        Path row;
        row.closed = false;
        bool anyOn = false;
        for (int x = 0; x < in.width_px; ++x)
        {
            uint8_t v = in.pixels[static_cast<size_t>(y) * static_cast<size_t>(in.width_px) + static_cast<size_t>(x)];
            if (v >= threshold)
            {
                anyOn = true;
                // Place points in local mm space at pixel centers
                float px = (static_cast<float>(x) + 0.5f) * in.pixel_size_mm;
                float py = (static_cast<float>(y) + 0.5f) * in.pixel_size_mm;
                row.points.emplace_back(px, py);
            }
        }
        if (anyOn && row.points.size() >= 2)
        {
            out.paths.push_back(std::move(row));
        }
    }

    out.computeAABB();
}


