#pragma once

#include <atomic>

#include "../Filter.h"

struct BlurFilter : public FilterTyped<Bitmap, Bitmap>
{
    int radiusPx{1};

    const char *name() const override { return "Blur"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setRadius(int r)
    {
        if (r < 0) r = 0;
        if (r != radiusPx)
        {
            radiusPx = r;
            m_version.fetch_add(1);
        }
    }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;


};


