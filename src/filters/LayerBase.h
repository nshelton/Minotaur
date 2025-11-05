#pragma once

enum class LayerKind
{
    Bitmap,
    PathSet,
    FloatImage
};

struct ILayerData
{
    virtual ~ILayerData() = default;
    virtual LayerKind kind() const = 0;
};


