#pragma once

#include <memory>
#include <cassert>

#include "core/Bitmap.h"
#include "core/Pathset.h"
#include "core/FloatImage.h"
#include "core/ColorImage.h"
#include "filters/LayerBase.h"

using BitmapPtr = std::shared_ptr<Bitmap>;
using PathSetPtr = std::shared_ptr<PathSet>;
using FloatImagePtr = std::shared_ptr<FloatImage>;
using ColorImagePtr = std::shared_ptr<ColorImage>;
using LayerPtr = std::shared_ptr<ILayerData>;

inline bool isBitmapLayer(const LayerPtr &p) { return p && p->kind() == LayerKind::Bitmap; }
inline bool isPathSetLayer(const LayerPtr &p) { return p && p->kind() == LayerKind::PathSet; }
inline bool isFloatImageLayer(const LayerPtr &p) { return p && p->kind() == LayerKind::FloatImage; }
inline bool isColorImageLayer(const LayerPtr &p) { return p && p->kind() == LayerKind::ColorImage; }

inline Bitmap *asBitmapPtr(const LayerPtr &p) { return isBitmapLayer(p) ? static_cast<Bitmap *>(p.get()) : nullptr; }
inline PathSet *asPathSetPtr(const LayerPtr &p) { return isPathSetLayer(p) ? static_cast<PathSet *>(p.get()) : nullptr; }
inline FloatImage *asFloatImagePtr(const LayerPtr &p) { return isFloatImageLayer(p) ? static_cast<FloatImage *>(p.get()) : nullptr; }
inline ColorImage *asColorImagePtr(const LayerPtr &p) { return isColorImageLayer(p) ? static_cast<ColorImage *>(p.get()) : nullptr; }

inline const Bitmap *asBitmapConstPtr(const LayerPtr &p) { return isBitmapLayer(p) ? static_cast<const Bitmap *>(p.get()) : nullptr; }
inline const PathSet *asPathSetConstPtr(const LayerPtr &p) { return isPathSetLayer(p) ? static_cast<const PathSet *>(p.get()) : nullptr; }
inline const FloatImage *asFloatImageConstPtr(const LayerPtr &p) { return isFloatImageLayer(p) ? static_cast<const FloatImage *>(p.get()) : nullptr; }
inline const ColorImage *asColorImageConstPtr(const LayerPtr &p) { return isColorImageLayer(p) ? static_cast<const ColorImage *>(p.get()) : nullptr; }

inline LayerPtr makeLayerFrom(const Bitmap &b) { return std::make_shared<Bitmap>(b); }
inline LayerPtr makeLayerFrom(const PathSet &ps) { return std::make_shared<PathSet>(ps); }
inline LayerPtr makeLayerFrom(const FloatImage &fi) { return std::make_shared<FloatImage>(fi); }
inline LayerPtr makeLayerFrom(const ColorImage &ci) { return std::make_shared<ColorImage>(ci); }


