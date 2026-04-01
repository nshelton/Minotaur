# Minotaur - Architecture Guide for AI Agents

Minotaur is a desktop application for converting raster images into vector paths and
driving AxiDraw pen plotters. It is a C++17 OpenGL application using ImGui for UI.

## Build

```bash
# macOS (primary dev platform)
cmake --preset macos-debug
cmake --build build --config Debug
./build/minotaur.app/Contents/MacOS/minotaur

# Windows
cmake --preset debug
cmake --build build --config Debug
build/Debug/minotaur.exe

# Tests
cmake --build build --target minotaur_tests motion_planner_tests filter_tests
ctest --test-dir build
```

Dependencies are managed via vcpkg manifest mode (`vcpkg.json`). Required: glfw3,
glad, imgui (with glfw+opengl3 bindings), fmt, glog, nlohmann-json, gtest, curl.

## Source Layout

```
src/
  main.cpp                  # Entry point: inits registries, boots App + MainScreen
  app/
    App.h/cpp               # GLFW/GLAD/ImGui init, 60fps render loop, input callbacks
    Screen.h                # IScreen interface (onAttach, onUpdate, onRender, onGui)
  core/                     # Value types - no dependencies on rest of codebase
    Vec2.h                  # 2D vector math
    Mat3.h                  # 3x3 column-major affine transforms
    Bitmap.h                # 8-bit grayscale image (width, height, pixel_size_mm, pixels)
    FloatImage.h            # 32-bit float image (same layout as Bitmap but float)
    ColorImage.h            # RGB 8-bit interleaved image
    Pathset.h               # Path (vector<Vec2> + closed flag), PathSet (vector<Path>)
    Color.h                 # RGBA float color
    Theme.h                 # Canonical UI colors for path/bitmap types
    entity.h                # Entity: payload + generator + FilterChain + transform
    Core.h                  # Convenience include for all core types
  filters/                  # The filter pipeline framework
    Filter.h                # FilterBase, FilterParameter, FilterTyped<In,Out> template
    FilterChain.h           # Ordered filter list with async eval + generation-based caching
    FilterRegistry.h/cpp    # Singleton factory registry for all filters
    Types.h                 # LayerKind enum, LayerPtr typedef, type helpers
    LayerBase.h             # ILayerData base for polymorphic layer handling
    bitmap/                 # Filters operating on raster images (20 filters)
    pathset/                # Filters operating on vector paths (7 filters)
  generators/               # Procedural content generators
    GeneratorBase.h         # GeneratorBase interface (sync + async models)
    GeneratorTyped.h        # CRTP template for strongly-typed generators
    GeneratorRegistry.h/cpp # Singleton factory registry for all generators
    bitmap/                 # Procedural bitmap generators (3 generators)
    pathset/                # Procedural path generators (6 generators)
    mesh/                   # 3D mesh to 2D projection (1 generator)
  render/
    LineRenderer.h/cpp      # GL line/point rendering (embedded GLSL shaders)
    BitmapRenderer.h/cpp    # GL texture quad for grayscale/RGB images
    FloatImageRenderer.h/cpp# FloatImage rendering with value-to-color mapping
  screens/
    MainScreen.h/cpp        # Main scene: page model, interaction, plotter control
    MainScreen.gui.cpp      # ImGui panel code (separate TU for build speed)
  plotters/
    MotionPlanner.h/cpp     # CoreXY trapezoidal velocity planner
    PlotSpooler.h/cpp       # Threaded job queue, path reordering, serial streaming
    AxidrawController.h/cpp # EBB protocol commands (pen, steppers, servo)
    PlotterConfig.h         # Kinematic parameters (speeds, accel, pen positions)
    PlotterManager.h/cpp    # High-level plotter facade (connect, job control, GUI)
  serial/
    SerialController.h/cpp  # Cross-platform USB serial (Win/macOS/Linux)
  utils/
    Serialization.h/cpp     # JSON project save/load (entities, filters, camera, plotter)
    ImageLoader.h/cpp       # Image loading (WIC on Windows, stb_image elsewhere)
    ImageCompression.h/cpp  # RLE + Base64 compression for bitmap serialization
    KdTree2D.h              # 2D spatial index (wraps nanoflann)
    VectorFont.h/cpp        # Stroke font for text-to-path
    PathSetGenerator.h/cpp  # Static helper functions for procedural shapes
    BitmapGenerator.h/cpp   # Static helper functions for procedural bitmaps
    HilbertCurve.h/cpp      # Space-filling curve for path ordering
    Clipboard.h             # Platform clipboard image access
    HttpFetcher.h           # HTTP GET via libcurl
    MvtDecoder.h            # Mapbox Vector Tile protobuf decoder
    ObjLoader.h             # Wavefront OBJ mesh loader
    MeshDecimator.h         # QEM mesh decimation
    MeshPreviewWidget.h/cpp # Inline ImGui 3D mesh preview
    stb_image.h             # stb_image single-header library
  Renderer.h/cpp            # Composite renderer dispatching by layer type
  Camera.h/cpp              # Orthographic pan/zoom camera (mm to NDC)
  Page.h/cpp                # PageModel: A3 (297x420mm), map<int, Entity>
  Interaction.h/cpp         # Selection, hover, drag, resize handles
tests/
  test_kdtree.cpp           # KdTree2D unit tests
  test_motion_planner.cpp   # MotionPlanner tests (28 cases, regression tests)
  test_blur_filter.cpp      # BlurFilter unit tests
  test_threshold_filter.cpp # ThresholdFilter unit tests
  test_levels_filter.cpp    # LevelsFilter unit tests
  test_trace_filter.cpp     # TraceFilter unit tests
  test_simplify_filter.cpp  # SimplifyFilter unit tests
```

## Core Architecture

See also: `docs/architecture.md` for detailed design documentation.

### Data Flow

```
Source (file drop, clipboard, or generator)
  -> Entity created in PageModel with payload (Bitmap/ColorImage/PathSet)
  -> Generator (if present) produces payload from parameters
  -> FilterChain applied: base layer -> filter1 -> filter2 -> ... -> output LayerPtr
  -> Renderer draws output (LineRenderer for paths, BitmapRenderer for images)
  -> User hits "Plot" -> PlotSpooler streams to AxiDraw via serial
```

### Layer Type System

Four layer types flow through the filter and generator pipelines:

| LayerKind    | C++ Type   | Description                        |
|------------- |----------- |----------------------------------- |
| Bitmap       | Bitmap     | 8-bit grayscale, row-major pixels  |
| FloatImage   | FloatImage | 32-bit float grayscale             |
| ColorImage   | ColorImage | RGB 8-bit interleaved              |
| PathSet      | PathSet    | Vector polylines in mm coordinates |

`LayerPtr` is `shared_ptr<ILayerData>`. Type checked at runtime via `LayerKind` enums.

### Entity Model

Each `Entity` in the `PageModel` has:
- A **payload** (`LayerPtr`) - the base data (imported or generated)
- An optional **generator** (`unique_ptr<GeneratorBase>`) - produces payload parametrically
- A **FilterChain** - ordered list of filters that transform the payload
- A **localToPage** `Mat3` transform - position/scale/rotation on the A3 canvas
- Visibility, color, name, unique ID

Entities are stored in `PageModel::entities` as `map<int, Entity>`.

### Coordinate System

All geometry is in **millimeters**. The page is A3 (297x420mm). Bitmaps scale via
`pixel_size_mm`. Camera converts mm to NDC for OpenGL rendering.

## Filter System

See also: `docs/filters-and-generators.md` for the complete filter/generator reference.

### How to Add a New Filter (3 steps)

**Step 1:** Create the filter header (and .cpp if needed) in `src/filters/bitmap/`
or `src/filters/pathset/`. Inherit from `FilterTyped<InputType, OutputType>`:

```cpp
// src/filters/bitmap/MyNewFilter.h
#pragma once
#include "filters/Filter.h"

struct MyNewFilter : public FilterTyped<Bitmap, PathSet> {
    MyNewFilter() {
        m_parameters["spacing"] = FilterParameter{"spacing", 0.5f, 20.0f, 3.0f};
    }

    const char *name() const override { return "My New Filter"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, PathSet &out) const override {
        float spacing = m_parameters.at("spacing").value;
        out.paths.clear();
        // ... your algorithm here ...
    }
};
```

**Step 2:** Register in `src/filters/FilterRegistry.cpp` inside `initDefaults()`:
```cpp
#include "filters/bitmap/MyNewFilter.h"
// ...
reg.registerFilter(FilterInfo{
    "My New Filter",
    LayerKind::Bitmap, LayerKind::PathSet,
    []() { return std::make_unique<MyNewFilter>(); }
});
```

**Step 3:** Done. The filter appears automatically in the UI dropdown. Serialization,
caching, and enable/disable are handled by FilterChain.

### How to Add a New Generator (3 steps)

**Step 1:** Create the generator in `src/generators/{bitmap,pathset,mesh}/`.
Use CRTP with `GeneratorTyped<Derived, OutputType>`:

```cpp
// src/generators/pathset/MyGenerator.h
#pragma once
#include "generators/GeneratorTyped.h"

struct MyGenerator : public GeneratorTyped<MyGenerator, PathSet> {
    MyGenerator() {
        m_parameters["radius"] = FilterParameter{"radius", 1.0f, 200.0f, 50.0f};
    }

    const char *name() const override { return "My Generator"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void generateTyped(PathSet &out) const override {
        out.paths.clear();
        // ... produce geometry ...
    }
};
```

For expensive generators (network, file I/O), override `isAsync()` to return `true`
and implement `startGenerate()`, `isReady()`, `collectResult()` instead.

**Step 2:** Register in `src/generators/GeneratorRegistry.cpp` inside `initDefaults()`.

**Step 3:** Done. The generator appears in the UI "Add Generator" menu.

### Key Rules for Filters and Generators

- `applyTyped()` / `generateTyped()` are `const` - do not mutate state during execution
- All parameters are `float` stored in `FilterParameter` with min/max/default
- `FilterParameter::Type` supports Float, Int, Bool, Enum, Precise display hints
- String parameters (generators only) via `m_stringParameters` map
- `m_version` (atomic counter) auto-increments on `setParameter()` calls
- `paramVersion()` must return `m_version.load()` for cache invalidation
- All coordinates are in millimeters; use `pixel_size_mm` to convert pixel indices

## Filter Chain Caching

`FilterChain` implements lazy evaluation with generation-based cache invalidation:
- Each filter output is cached in a `LayerCache` with upstream generation + param version
- Changing a parameter bumps `m_version`, invalidating downstream caches
- Evaluation runs asynchronously via `std::async`; `output()` is non-blocking
- `outputBlocking()` waits for completion (used when plotting)
- Disabled filters are bypassed (upstream data passes through unchanged)
- The chain validates type compatibility when adding/removing/toggling filters

## Rendering

OpenGL 3.3 Core Profile. All shaders are embedded as C++ string literals in renderers.
- `LineRenderer`: Renders PathSet as GL_LINES + GL_POINTS with point sprites
- `BitmapRenderer`: Renders Bitmap/ColorImage as textured quads (GL_R8 or GL_RGB8)
- `FloatImageRenderer`: Renders FloatImage with value-to-color mapping
- `Renderer`: Composite facade dispatching entities by layer type
- `Camera`: Orthographic projection (mm to NDC), pan/zoom with mouse

No separate shader files exist. To modify shaders, edit the string literals in
the renderer `.cpp` files.

## Plotter Pipeline

The plotting stack converts vector paths to physical AxiDraw pen movements:

```
PathSet (mm coordinates)
  -> PlotterManager: high-level facade (connect, start/pause/cancel)
    -> PlotSpooler: reorders paths (nearest-neighbor or Hilbert), manages job queue
      -> MotionPlanner: trapezoidal velocity planning, junction deviation for corners
        -> MoveSlice (aSteps, bSteps, dtMs) per time slice
          -> AxidrawController: EBB protocol (SM, SP, SC commands)
            -> SerialController: USB serial I/O to hardware
```

`PlotSpooler` runs a background worker thread. It buffers commands with high/low
watermarks (~1200ms / ~300ms queued) to keep the plotter fed without overflowing.
Real-time progress is tracked and remaining paths are rendered as an orange overlay.

CoreXY kinematics: A = dx + dy, B = dx - dy (stepper motor mapping).

## Serialization

Project state saves to JSON (`page.json`) via `src/utils/Serialization.cpp`.
Saved state includes: all entities (with payload data), filter chains (filter type
name + parameters + enabled state), generator state (type name + float/string params),
camera position/zoom, plotter config. Bitmap data uses RLE + Base64 compression.
The file can be large (10+ MB with image data).

## Conventions

- **C++17** throughout, no Boost
- **Tabs for indentation** in most files
- **Allman brace style** (opening brace on new line)
- Headers use `#pragma once`
- Logging via `glog` (`LOG(INFO)`, `LOG(ERROR)`)
- String formatting via `fmt::format()`
- No SVG or GCode export exists - output is AxiDraw-only via serial
- `page.json` in the repo root is a working project file (large, binary image data)

## Testing

Three test targets exist:
- `minotaur_tests` - KdTree2D spatial indexing tests
- `motion_planner_tests` - 28 test cases for MotionPlanner (velocity profiles,
  position conservation, junction deviation, step rate limits)
- `filter_tests` - Unit tests for BlurFilter, ThresholdFilter, LevelsFilter,
  TraceFilter, SimplifyFilter

All use Google Test. Run with `ctest --test-dir build`.

## Key Gotchas

- `MainScreen.gui.cpp` is a separate compilation unit from `MainScreen.cpp` to
  speed up incremental builds
- Filter `applyTyped()` is `const` - filters must not mutate their own state during
  execution (stats like progress/timing are written via atomics on the base class)
- `FilterParameter` values are `float` - use the `Type` enum (Int, Bool, Enum,
  Precise) to control UI rendering, and cast in the filter implementation
- The CMake build uses `GLOB_RECURSE` for source files - new `.cpp` files in `src/`
  are automatically picked up, but CMake must be re-run to detect them
- `Renderer.h/cpp` and `Camera.h/cpp` live directly in `src/` rather than `src/render/`
- Some utils (`HttpFetcher.h`, `MvtDecoder.h`, `ObjLoader.h`, `MeshDecimator.h`)
  are header-only implementations
