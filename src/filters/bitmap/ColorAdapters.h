#pragma once

#include <string>

#include "../Filter.h"

// Adapters that lift grayscale filters onto RGB images without duplicating
// their algorithms. The wrapped filter is instantiated per evaluation and fed
// this adapter's parameter map, so applyTyped() stays const and thread-safe.

namespace coloradapt
{
    // Copy one interleaved RGB channel out into a tightly packed grayscale plane
    inline void extractChannel(const ColorImage &in, int channel, Bitmap &out)
    {
        out.width_px = in.width_px;
        out.height_px = in.height_px;
        out.pixel_size_mm = in.pixel_size_mm;

        const size_t numPixels = in.width_px * in.height_px;
        out.pixels.resize(numPixels);
        for (size_t i = 0; i < numPixels; ++i)
        {
            out.pixels[i] = in.pixels[i * 3 + static_cast<size_t>(channel)];
        }
    }

    // Write a grayscale plane back into one interleaved RGB channel
    inline void storeChannel(const Bitmap &plane, int channel, ColorImage &out)
    {
        const size_t numPixels = out.width_px * out.height_px;
        if (plane.pixels.size() < numPixels || out.pixels.size() < numPixels * 3)
        {
            return;
        }
        for (size_t i = 0; i < numPixels; ++i)
        {
            out.pixels[i * 3 + static_cast<size_t>(channel)] = plane.pixels[i];
        }
    }
}

// Display name of the RGB lift of a grayscale filter. Single source of truth:
// project loading resolves filters by name alone, so FilterRegistry must
// register exactly this string.
template <typename GrayFilter>
inline std::string perChannelColorName()
{
    GrayFilter proto;
    return std::string(proto.name()) + " (RGB)";
}

// Runs a Bitmap -> Bitmap filter independently on the R, G and B channels.
// Correct for filters whose channels are genuinely independent (blur, levels,
// threshold); not appropriate for histogram-based filters such as CLAHE, which
// need a luminance-only formulation to avoid color casts.
template <typename GrayFilter>
struct PerChannelColorFilter : public FilterTyped<ColorImage, ColorImage>
{
    PerChannelColorFilter()
    {
        GrayFilter proto;
        m_parameters = proto.m_parameters;
        m_name = perChannelColorName<GrayFilter>();
    }

    const char *name() const override { return m_name.c_str(); }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const ColorImage &in, ColorImage &out) const override
    {
        const size_t numPixels = in.width_px * in.height_px;
        if (numPixels == 0 || in.pixels.size() < numPixels * 3)
        {
            out.width_px = 0;
            out.height_px = 0;
            out.pixel_size_mm = in.pixel_size_mm;
            out.pixels.clear();
            return;
        }

        GrayFilter f;
        f.m_parameters = m_parameters;

        Bitmap plane;
        Bitmap result;
        for (int c = 0; c < 3; ++c)
        {
            coloradapt::extractChannel(in, c, plane);
            f.applyTyped(plane, result);

            if (c == 0)
            {
                out.width_px = result.width_px;
                out.height_px = result.height_px;
                out.pixel_size_mm = result.pixel_size_mm;
                out.pixels.assign(result.width_px * result.height_px * 3, 0);
            }
            else if (result.width_px != out.width_px || result.height_px != out.height_px)
            {
                LOG(ERROR) << m_name << ": channel " << c << " produced "
                           << result.width_px << "x" << result.height_px
                           << ", expected " << out.width_px << "x" << out.height_px
                           << "; aborting";
                out.pixels.clear();
                out.width_px = 0;
                out.height_px = 0;
                return;
            }

            coloradapt::storeChannel(result, c, out);
            setProgress(static_cast<float>(c + 1) / 3.0f);
        }
    }

private:
    std::string m_name;
};
