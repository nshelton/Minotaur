#pragma once

enum class LayerKind
{
    Bitmap,
    PathSet,
    FloatImage,
    ColorImage
};

struct ILayerData
{
    virtual ~ILayerData() = default;
    virtual LayerKind kind() const = 0;
};


