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
cmake --build build --target minotaur_tests motion_planner_tests
ctest --test-dir build
```

Dependencies are managed via vcpkg manifest mode (`vcpkg.json`). Required: glfw3,
glad, imgui (with glfw+opengl3 bindings), fmt, glog, nlohmann-json, gtest.

## Source Layout

```
src/
  main.cpp                  # Entry point: inits FilterRegistry, boots App + MainScreen
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
    entity.h                # Entity: payload variant + transform + FilterChain
    Core.h                  # Convenience include for all core types
  filters/                  # The filter pipeline framework
    Filter.h                # FilterBase interface, FilterTyped<In,Out> template
    FilterChain.h           # Ordered filter list with lazy caching
    FilterRegistry.h/cpp    # Singleton factory registry for all filters
    Types.h                 # LayerKind enum, LayerPtr typedef, type helpers
    LayerBase.h             # ILayerData base for polymorphic layer handling
    bitmap/                 # Filters operating on raster images (18 filters)
    pathset/                # Filters operating on vector paths (6 filters)
  render/
    LineRenderer.h/cpp      # GL line/point rendering (embedded GLSL shaders)
    BitmapRenderer.h/cpp    # GL texture quad for grayscale/RGB images
    FloatImageRenderer.h/cpp
    Renderer.h/cpp          # Composite renderer dispatching by layer type
    Camera.h/cpp            # Orthographic pan/zoom camera (mm to NDC)
  screens/
    MainScreen.h/cpp        # Main scene: page model, interaction, plotter control
    MainScreen.gui.cpp      # ImGui panel code (separate TU for build speed)
  plotters/
    MotionPlanner.h/cpp     # CoreXY trapezoidal velocity planner
    PlotSpooler.h/cpp       # Threaded job queue, path reordering, serial streaming
    AxidrawController.h/cpp # EBB protocol commands (pen, steppers, servo)
    PlotterConfig.h         # Kinematic parameters (speeds, accel, pen positions)
  serial/
    SerialController.h/cpp  # Cross-platform USB serial (Win/macOS/Linux)
  utils/
    Serialization.h/cpp     # JSON project save/load (entities, filters, camera, plotter)
    ImageLoader.h/cpp       # stb_image-based loading (PNG/JPEG/BMP/PGM)
    KdTree2D.h              # 2D spatial index (wraps nanoflann)
    VectorFont.h/cpp        # Stroke font for text-to-path
    PathSetGenerator.h/cpp  # Procedural shapes (circle, square, star, text)
    BitmapGenerator.h/cpp   # Procedural bitmap generation
    HilbertCurve.h/cpp      # Space-filling curve for path ordering
    ImageCompression.h/cpp  # RLE compression for bitmap serialization
  Page.h/cpp                # PageModel: A3 (297x420mm), map<int, Entity>
  Interaction.h/cpp         # Selection, hover, drag, resize handles
tests/
  test_kdtree.cpp           # KdTree2D unit tests
  test_motion_planner.cpp   # MotionPlanner tests (28 cases, regression tests)
```

## Core Architecture

### Data Flow

```
Image file dropped / loaded
  -> Entity created in PageModel with payload (Bitmap/ColorImage/PathSet)
  -> FilterChain applied: base layer -> filter1 -> filter2 -> ... -> output LayerPtr
  -> Renderer draws output (LineRenderer for paths, BitmapRenderer for images)
  -> User hits "Plot" -> PlotSpooler streams to AxiDraw via serial
```

### Layer Type System

Four layer types flow through the filter pipeline:

| LayerKind    | C++ Type   | Description                        |
|------------- |----------- |----------------------------------- |
| Bitmap       | Bitmap     | 8-bit grayscale, row-major pixels  |
| FloatImage   | FloatImage | 32-bit float grayscale             |
| ColorImage   | ColorImage | RGB 8-bit interleaved              |
| PathSet      | PathSet    | Vector polylines in mm coordinates |

`LayerPtr` is `shared_ptr<ILayerData>`. Type checked at runtime via `LayerKind` enums.

### Entity Model

Each `Entity` in the `PageModel` has:
- A **payload** (variant of the 4 layer types) - the raw imported data
- A **FilterChain** - ordered list of filters that transform the payload
- A **localToPage** Mat3 transform - position/scale/rotation on the A3 canvas
- Visibility, color, name, unique ID

### Coordinate System

All geometry is in **millimeters**. The page is A3 (297x420mm). Bitmaps scale via
`pixel_size_mm`. Camera converts mm to NDC for OpenGL rendering.

## Filter System - How to Add a New Filter

This is the most common contribution pattern. Follow these 3 steps:

### Step 1: Create the filter (header + implementation)

Place in `src/filters/bitmap/` or `src/filters/pathset/` depending on domain.
Inherit from `FilterTyped<InputType, OutputType>`:

```cpp
// src/filters/bitmap/MyNewFilter.h
#pragma once
#include "filters/Filter.h"

struct MyNewFilter : public FilterTyped<Bitmap, PathSet> {
    MyNewFilter() {
        // Define UI-exposed parameters with name, min, max, default
        m_parameters["spacing"] = FilterParameter{"spacing", 0.5f, 20.0f, 3.0f};
    }

    const char *name() const override { return "My New Filter"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, PathSet &out) const override {
        float spacing = m_parameters.at("spacing").value;
        out.paths.clear();
        // ... your algorithm here, read pixels from in, write paths to out ...
    }
};
```

Key rules:
- Parameters are `float` with min/max range - ImGui renders them as sliders
- `m_version` is an atomic counter; it auto-increments when `setParameter()` is called
- `paramVersion()` must return `m_version.load()` for cache invalidation to work
- Copy metadata from input (e.g., `out.paths.clear()` before writing)
- All coordinates are in millimeters; use `in.pixel_size_mm` to convert pixel indices

### Step 2: Register in FilterRegistry

In `src/filters/FilterRegistry.cpp`:
1. Add `#include "filters/bitmap/MyNewFilter.h"` at the top
2. Add registration call inside `initDefaults()`:
```cpp
reg.registerFilter(FilterInfo{
    "My New Filter",
    LayerKind::Bitmap,    // must match FilterTyped template args
    LayerKind::PathSet,
    []() { return std::make_unique<MyNewFilter>(); }
});
```

### Step 3: That's it

No other wiring needed. The filter automatically appears in the UI dropdown for
entities whose current output matches the filter's input type. Serialization,
caching, and enable/disable are handled by FilterChain.

## Filter Chain Caching

`FilterChain` implements lazy evaluation with generation-based cache invalidation:
- Each filter output is cached in a `LayerCache` with upstream generation + param version
- Changing a parameter (via `setParameter`) bumps `m_version`, invalidating the cache
- Upstream changes propagate generation counters to invalidate downstream caches
- Disabled filters are bypassed (upstream data passes through unchanged)
- The chain validates type compatibility when adding/removing/toggling filters

## Rendering

OpenGL 3.3 Core Profile. All shaders are embedded as C++ string literals in renderers.
- `LineRenderer`: Renders PathSet as GL_LINES + GL_POINTS with point sprites
- `BitmapRenderer`: Renders Bitmap/ColorImage as textured quads (GL_R8 or GL_RGB8)
- `FloatImageRenderer`: Renders FloatImage with value-to-color mapping
- Camera provides an orthographic Mat3 projection (mm -> NDC)

No separate shader files exist. To modify shaders, edit the string literals in
the renderer `.cpp` files.

## Plotter Pipeline

The plotting stack converts vector paths to physical AxiDraw pen movements:

```
PathSet (mm coordinates)
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
name + parameters + enabled state), camera position/zoom, plotter config. Bitmap
data uses RLE compression. The file can be large (10+ MB with image data).

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

Two test targets exist:
- `minotaur_tests` - KdTree2D spatial indexing tests
- `motion_planner_tests` - 28 test cases for MotionPlanner (velocity profiles,
  position conservation, junction deviation, step rate limits)

Both use Google Test. Run with `ctest --test-dir build`.

## Key Gotchas

- `MainScreen.gui.cpp` is a separate compilation unit from `MainScreen.cpp` to
  speed up incremental builds - the GUI code is ~28KB
- Filter `apply()` is `const` - filters must not mutate their own state during execution
  (parameters are read-only during apply, stats are written via atomics)
- All `FilterParameter` values are `float` - there is no support for int/bool/enum
  parameter types (use float ranges and cast in the filter)
- The CMake build uses `GLOB_RECURSE` for source files - new `.cpp` files in `src/`
  are automatically picked up, but CMake must be re-run to detect them