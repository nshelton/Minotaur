#pragma once

#include <memory>
#include <cassert>

#include "core/Bitmap.h"
#include "core/Pathset.h"
#include "filters/LayerBase.h"

using BitmapPtr = std::shared_ptr<Bitmap>;
using PathSetPtr = std::shared_ptr<PathSet>;
using LayerPtr = std::shared_ptr<ILayerData>;

inline bool isBitmapLayer(const LayerPtr &p) { return p && p->kind() == LayerKind::Bitmap; }
inline bool isPathSetLayer(const LayerPtr &p) { return p && p->kind() == LayerKind::PathSet; }

inline Bitmap *asBitmapPtr(const LayerPtr &p) { return isBitmapLayer(p) ? static_cast<Bitmap *>(p.get()) : nullptr; }
inline PathSet *asPathSetPtr(const LayerPtr &p) { return isPathSetLayer(p) ? static_cast<PathSet *>(p.get()) : nullptr; }

inline const Bitmap *asBitmapConstPtr(const LayerPtr &p) { return isBitmapLayer(p) ? static_cast<const Bitmap *>(p.get()) : nullptr; }
inline const PathSet *asPathSetConstPtr(const LayerPtr &p) { return isPathSetLayer(p) ? static_cast<const PathSet *>(p.get()) : nullptr; }

inline LayerPtr makeLayerFrom(const Bitmap &b) { return std::make_shared<Bitmap>(b); }
inline LayerPtr makeLayerFrom(const PathSet &ps) { return std::make_shared<PathSet>(ps); }


