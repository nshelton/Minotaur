#pragma once

#include "../Filter.h"

struct CurlNoiseFilter : public FilterTyped<PathSet, PathSet>
{
    CurlNoiseFilter()
    {
        m_parameters["amplitudeMm"] = FilterParameter{
            "Amplitude (mm)",
            0.0f,
            50.0f,
            5.0f
        };
        m_parameters["scaleMm"] = FilterParameter{
            "Scale (mm)",
            1.0f,
            200.0f,
            50.0f
        };
        m_parameters["octaves"] = FilterParameter{
            "Octaves",
            1.0f,
            8.0f,
            3.0f
        };
        m_parameters["lacunarity"] = FilterParameter{
            "Lacunarity",
            1.0f,
            4.0f,
            2.0f
        };
        m_parameters["gain"] = FilterParameter{
            "Gain",
            0.0f,
            1.0f,
            0.5f
        };
        m_parameters["seed"] = FilterParameter{
            "Seed",
            0.0f,
            1000.0f,
            0.0f
        };
    }

    const char *name() const override { return "Curl Noise Displace"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const PathSet &in, PathSet &out) const override;
};


