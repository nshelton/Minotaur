#pragma once

enum class LayerKind
{
    Bitmap,
    PathSet
};

struct ILayerData
{
    virtual ~ILayerData() = default;
    virtual LayerKind kind() const = 0;
};


