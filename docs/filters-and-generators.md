# Filters and Generators Reference

This document catalogs every filter and generator in Minotaur, their type
signatures, parameters, and algorithms. It also explains the framework
architecture that makes them extensible.

---

## Filter Framework Architecture

### Class Hierarchy

```
FilterBase (abstract)
  └── FilterTyped<InT, OutT> (template)
        ├── BlurFilter           : FilterTyped<Bitmap, Bitmap>
        ├── LineHatchFilter      : FilterTyped<Bitmap, PathSet>
        ├── ChannelMixerFilter   : FilterTyped<ColorImage, Bitmap>
        ├── SimplifyFilter       : FilterTyped<PathSet, PathSet>
        └── ... (27 total)
```

### Parameter System

All parameters are `FilterParameter` structs stored in `m_parameters` (a
`map<string, FilterParameter>`):

```cpp
struct FilterParameter {
    enum Type { Float, Int, Bool, Enum, Precise };
    string name;
    float minValue, maxValue, value;
    Type type;
    vector<string> enumLabels;  // Enum type only
};
```

The `Type` field controls ImGui rendering:
- **Float**: Continuous slider between min/max
- **Int**: Integer-snapping slider
- **Bool**: Checkbox (value 0.0 or 1.0)
- **Enum**: Dropdown populated from `enumLabels`
- **Precise**: High-precision slider (for coordinates, etc.)

Calling `setParameter(key, value)` updates the value and increments the atomic
`m_version` counter, which triggers cache invalidation in the FilterChain.

### Execution Model

- `apply()` / `applyTyped()` are **const** methods - filters must not mutate state
- Progress and timing are reported via atomic members on FilterBase:
  `setProgress(0..1)`, `setRunning(bool)`, `setLastRunMs(double)`
- Output statistics (vertex/path counts) are tracked for PathSet outputs

### FilterChain Integration

When a filter is registered in `FilterRegistry`, the framework handles:
1. **UI**: Filter appears in the "Add Filter" dropdown, filtered by compatible
   input type
2. **Serialization**: Save/load by name with parameter values
3. **Caching**: Lazy evaluation with generation-based invalidation
4. **Enable/disable**: Toggle with type-safety checks
5. **Statistics**: Run time, vertex counts, progress displayed per filter

---

## Bitmap Filters (20 total)

Filters that operate on raster image data. Input is always `Bitmap` or
`ColorImage`; output varies.

### Image Adjustment

| Filter | Signature | Parameters | Description |
|--------|-----------|------------|-------------|
| **Levels** | Bitmap -> Bitmap | bias (-1..1), gain (0..4), invert (Bool) | Linear tone mapping: `(pixel/255 + bias) * gain` |
| **CLAHE** | Bitmap -> Bitmap | tilesX (1..64), tilesY (1..64), clipLimit (0..4) | Contrast Limited Adaptive Histogram Equalization |
| **Blur** | Bitmap -> Bitmap | radius (0..10 px, Int) | Gaussian blur |
| **Rotate** | Bitmap -> Bitmap | angle (-180..180 deg) | Arbitrary rotation with bilinear interpolation |
| **Threshold** | Bitmap -> Bitmap | min (0..255, Int), max (0..255, Int) | Binary threshold: 255 if min <= pixel <= max, else 0 |
| **Erode/Dilate** | Bitmap -> Bitmap | operation (Enum: Erode/Dilate), iterations (1..8, Int) | Morphological operations |
| **Skeletonize** | Bitmap -> Bitmap | _(none)_ | Reduces shapes to 1-pixel-wide skeleton |

### Color to Grayscale

| Filter | Signature | Parameters | Description |
|--------|-----------|------------|-------------|
| **RGB to Grayscale** | ColorImage -> Bitmap | r (0..1), g (0..1), b (0..1) | Weighted channel mix (default: Rec.709 luminance) |
| **Color Picker** | ColorImage -> Bitmap | r/g/b (0..255), threshold (0..441) | Euclidean RGB distance mask |

### Edge Detection

| Filter | Signature | Parameters | Description |
|--------|-----------|------------|-------------|
| **Canny** | Bitmap -> Bitmap | blur_radius_px (0..5), low/high threshold (0..255) | Canny edge detection with hysteresis |

### Bitmap to PathSet (Vectorization)

These are the core raster-to-vector conversion filters:

| Filter | Signature | Parameters | Description |
|--------|-----------|------------|-------------|
| **Trace** | Bitmap -> PathSet | threshold (0..255, Int) | Simple row-based threshold tracer |
| **Blobs** | Bitmap -> PathSet | tolerancePx (0..10), turdSizePx (0..100), traceHoles (Bool) | Connected-component blob tracing with RDP simplification |
| **Line Hatch** | Bitmap -> PathSet | step_px (1..20, Int), angle_deg (-180..180), threshold (0..255, Int) | Parallel line hatching over dark regions |
| **Flow Field Hatch** | Bitmap -> PathSet | flow_field_type (Enum), threshold, seed_spacing_px, max_connect_distance_px, flow_strength, hatch_angle_deg, clamp_radius_px | Seeds dark pixels on grid, chains along flow-derived direction field |
| **Flow Snake** | Bitmap -> PathSet | step_size, momentum, max_turn, look_ahead, fov, eat_radius, eat_strength, max_steps | Agent-based: single snake attracted to dark pixels, eating ink as it moves |
| **Concentric Outline** | Bitmap -> PathSet | threshold (0..255, Int), spacing_px (1..50, Int) | Distance transform contours at regular intervals |
| **Voronoi Stippling** | Bitmap -> PathSet | num_points (10..100k, Int), iterations (1..100, Int), circle_radius, size_variation, circle_segments | Lloyd's relaxation stippling with circles |

### Intermediate Types

| Filter | Signature | Parameters | Description |
|--------|-----------|------------|-------------|
| **Bitmap Distance Field** | Bitmap -> FloatImage | threshold (0..1) | Chamfer distance transform (signed distance field) |
| **Float Blur** | FloatImage -> FloatImage | radius (0..20) | Gaussian blur on float data |
| **Float Maxima to Paths** | FloatImage -> PathSet | maximaRadius (1..7, Int), connectRadius (1..7, Int) | Detects local maxima on distance field ridges, connects nearby peaks |

---

## PathSet Filters (7 total)

All operate on PathSet -> PathSet.

| Filter | Parameters | Description |
|--------|------------|-------------|
| **Simplify** | toleranceMm (0..1), minPathLengthMm (0..10) | Ramer-Douglas-Peucker simplification + short path removal |
| **Smooth** | iterations (0..10, Int) | Catmull-Rom-style smooth interpolation |
| **Laplacian Smooth** | iterations (0..50, Int), weight (0..1) | Vertex averaging toward neighbors |
| **Regular Subdivision** | spacing (0.1..10 mm) | Ensures at least one vertex every N mm |
| **Curl Noise Displace** | amplitudeMm (0..50), scaleMm (1..200), octaves (1..8, Int), lacunarity (1..4), gain (0..1), seed (0..1000, Int) | Displaces points via multi-octave Perlin curl noise |
| **Optimize Paths** | mergeEnabled (Bool), mergeDistanceMm (0..50), reorder (Bool) | Merge nearby endpoints + reorder for minimal pen travel |

---

## Generator Framework Architecture

### Class Hierarchy

```
GeneratorBase (abstract)
  └── GeneratorTyped<Derived, OutT> (CRTP template)
        ├── CircleGenerator      : GeneratorTyped<CircleGenerator, PathSet>
        ├── GradientGenerator    : GeneratorTyped<GradientGenerator, Bitmap>
        ├── MeshGenerator        : GeneratorTyped<MeshGenerator, PathSet>  (async)
        ├── OsmGenerator         : GeneratorTyped<OsmGenerator, PathSet>   (async)
        └── ... (10 total)
```

### Sync vs Async

**Synchronous** (default): `generateTyped(out)` runs on the render thread.
Used for cheap procedural shapes.

**Asynchronous** (`isAsync() == true`): For expensive work:
1. `startGenerate()` launches background thread, returns immediately
2. `isReady()` polled each frame (non-blocking)
3. `collectResult(out)` transfers completed result to render thread
4. Must support cancel/restart (calling `startGenerate()` again before completion)

### String Parameters

Unlike filters, generators support string parameters (`m_stringParameters`) for
text input, file paths, etc. The `onStringParameterChanged(key)` hook allows
generators to react (e.g., reload a file).

### Clone Support

Generators implement `clone()` for entity duplication. The default CRTP
implementation copies all float and string parameters. Generators with extra
state (loaded meshes, cached tiles) must override `clone()`.

---

## PathSet Generators (6 total)

| Generator | Parameters | Description |
|-----------|------------|-------------|
| **Circle** | radius_mm (1..200), segments (3..256, Int) | Regular polygon circle |
| **Square** | side_mm (1..400) | Axis-aligned square centered at origin |
| **Star** | outer_radius_mm (1..200), inner_radius_mm (1..200), points (2..20, Int) | N-pointed star |
| **Text** | height_mm (1..100), letter_spacing (0..10); string: `text` | Stroke font text rendering via VectorFont |
| **Grid** | width_mm, height_mm, num_lines (Int), direction (Enum: H/V/Both) | Evenly-spaced parallel lines |
| **OSM Tiles** (async) | lat, lon (Precise), zoom (Int), size_mm, tile_count (Int), clipping params, layer toggles (9 Bool params) | Fetches OpenFreeMap MVT tiles, decodes protobuf, converts Mercator to mm |

## Bitmap Generators (3 total)

| Generator | Parameters | Description |
|-----------|------------|-------------|
| **Gradient** | width_px, height_px (Int), pixel_size_mm | Horizontal grayscale gradient |
| **Checkerboard** | width_px, height_px, block_px (Int), pixel_size_mm | Alternating 200/40 grayscale blocks |
| **Radial** | width_px, height_px (Int), pixel_size_mm | Radial gradient (white center, black edges) |

## Mesh Generator (1 total)

| Generator | Parameters | Description |
|-----------|------------|-------------|
| **Mesh** | rotX/Y/Z (-180..180), scale_mm, projection (Enum: Ortho/Perspective), mode (Enum: Wireframe/IsolineX/Y/Z), decimation (0..1), hiddenLines (Bool), depthBias, isoResolution; string: `file` | Loads OBJ, applies QEM decimation, projects to 2D with optional hidden line removal |

---

## Type Conversion Graph

This graph shows all possible type transitions through filters:

```
ColorImage ──[ChannelMixer]──> Bitmap
ColorImage ──[ColorPicker]───> Bitmap

Bitmap ──[Levels/Blur/CLAHE/Threshold/Rotate/ErodeDilate/Skeletonize/Canny]──> Bitmap

Bitmap ──[Trace]──────────────> PathSet
Bitmap ──[Blobs]──────────────> PathSet
Bitmap ──[LineHatch]──────────> PathSet
Bitmap ──[FlowFieldHatch]────> PathSet
Bitmap ──[FlowSnake]─────────> PathSet
Bitmap ──[ConcentricOutline]─> PathSet
Bitmap ──[VoronoiStippling]──> PathSet

Bitmap ──[BitmapDistanceField]──> FloatImage
FloatImage ──[FloatBlur]────────> FloatImage
FloatImage ──[FloatMaximaToPaths]──> PathSet

PathSet ──[Simplify/Smooth/LaplacianSmooth/RegularSubdivision/
           CurlNoise/OptimizePaths]──> PathSet
```

**Terminal type**: PathSet is the only type that can be plotted. All pipelines
that end in plotting must eventually convert to PathSet.

---

## Adding a New Filter: Checklist

1. Create `src/filters/{bitmap,pathset}/MyFilter.h` (and `.cpp` if needed)
2. Inherit from `FilterTyped<InputType, OutputType>`
3. Define parameters in constructor
4. Implement `name()`, `paramVersion()`, `applyTyped()`
5. Register in `FilterRegistry::initDefaults()` in `src/filters/FilterRegistry.cpp`
6. (Optional) Add unit test in `tests/test_my_filter.cpp` and register in CMakeLists.txt

## Adding a New Generator: Checklist

1. Create `src/generators/{bitmap,pathset,mesh}/MyGenerator.h` (and `.cpp` if needed)
2. Inherit from `GeneratorTyped<MyGenerator, OutputType>`
3. Define float params in constructor, string params if needed
4. Implement `name()`, `paramVersion()`, `generateTyped()` (or async methods)
5. Register in `GeneratorRegistry::initDefaults()` in `src/generators/GeneratorRegistry.cpp`
