#pragma once

#include "../Filter.h"

// CMYK ink separation for plotting with subtractive (marker / pen) inks.
//
// Color Picker answers "which pixels are close to this color?", which is the wrong
// question for subtractive media. A green region is not close to cyan and not close
// to yellow, so neither pen ever visits it and the green never appears on paper.
// Real CMYK works the other way round: green is *made* by laying cyan and yellow on
// top of one another.
//
// This filter solves that inverse problem. Given a target color and the measured
// colors of the actual inks, it asks how much of each ink has to overlap to
// reproduce it, and emits the coverage map for one of the four plates. Run one
// instance per pen (Channel = Cyan / Magenta / Yellow / Black) on four copies of the
// same image and plot the four separations as four passes.
//
// Output uses the usual "dark = ink" convention, so it feeds straight into Line
// Hatch / Hatch V2 / Voronoi Stippling with no inversion needed.
//
// IMPORTANT: only `channel` should differ between the four instances. The other
// parameters take part in the solve, so mismatched values across the plates produce
// separations that do not recombine into the original image.
struct CmykSeparationFilter : public FilterTyped<ColorImage, Bitmap>
{
    CmykSeparationFilter()
    {
        m_parameters["channel"] = FilterParameter{
            "Channel",
            0.0f,
            3.0f,
            0.0f,
            FilterParameter::Enum,
            {"Cyan", "Magenta", "Yellow", "Black"}
        };
        m_parameters["ink_profile"] = FilterParameter{
            "Ink Profile",
            0.0f,
            1.0f,
            1.0f,
            FilterParameter::Enum,
            {"Ideal CMY", "Marker CMY"}
        };
        m_parameters["mixing"] = FilterParameter{
            "Mixing Model",
            0.0f,
            1.0f,
            0.0f,
            FilterParameter::Enum,
            {"Area Coverage", "Optical Density"}
        };
        // Fraction of the neutral (gray) component moved out of C+M+Y and into the
        // black pen. 0 = no black plate at all, 1 = full gray component replacement.
        m_parameters["gcr"] = FilterParameter{
            "Black (GCR)",
            0.0f,
            1.0f,
            0.5f
        };
        // Cap on total coverage summed over all four plates. Four full passes of
        // marker ink will pulp the paper long before it reaches 4.0.
        m_parameters["ink_limit"] = FilterParameter{
            "Ink Limit",
            1.0f,
            4.0f,
            2.6f
        };
        // Response curve for this pen: <1 lays down more ink, >1 lays down less.
        m_parameters["gamma"] = FilterParameter{
            "Coverage Gamma",
            0.2f,
            3.0f,
            1.0f
        };
    }

    const char *name() const override { return "CMYK Separation"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const ColorImage &in, Bitmap &out) const override;
};
