# Minotaur Architecture

## Overview

Minotaur is a C++17 desktop application for converting raster images into vector
paths suitable for pen plotters (specifically the AxiDraw). The application follows
a pipeline architecture:

```
Input Sources -> Entity Model -> Filter Pipeline -> Rendering -> Plotter Output
```

This document covers the system design, data flow, component responsibilities,
and architectural tradeoffs.

---

## 1. Application Shell

### App (`src/app/App.h`)

The `App` class owns the GLFW window, initializes GLAD and ImGui, and runs the
main loop at ~60fps. It delegates all logic to an `IScreen` implementation via
callbacks. GLFW callbacks are routed through static functions that look up the
active screen from the window user pointer.

### IScreen (`src/app/Screen.h`)

Interface for application screens. Methods:
- `onAttach(App&)` / `onDetach()` - lifecycle
- `onUpdate(dt)` - logic tick
- `onRender()` - OpenGL draw calls
- `onGui()` - ImGui widget rendering
- Input: `onMouseButton`, `onCursorPos`, `onScroll`, `onFilesDropped`, `onClipboardPaste`

### MainScreen (`src/screens/MainScreen.h`)

The single concrete screen. Owns all application state:
- `PageModel` - the document model
- `Camera` - orthographic view
- `Renderer` - OpenGL drawing
- `InteractionController` - mouse/selection logic
- `PlotterManager` - hardware connection and job control

The GUI code lives in a separate translation unit (`MainScreen.gui.cpp`) to speed
up incremental builds.

### Startup (`src/main.cpp`)

```
main() -> FilterRegistry::initDefaults()
       -> GeneratorRegistry::initDefaults()
       -> App(2200, 1600, "Minotaur")
       -> MainScreen(projectPath)
       -> app.run(screen)
```

Registries must be initialized before any entities are created or deserialized.

---

## 2. Core Data Types

All types live in `src/core/` and have no dependencies on the rest of the codebase.

### Geometry

- **Vec2** (`Vec2.h`): 2D float vector with arithmetic operators, dot/cross/length.
- **Mat3** (`Mat3.h`): 3x3 column-major matrix for 2D affine transforms.
  Supports setOrtho, setTranslate, setScale, compose, applyInverse.
- **BoundingBox**: Axis-aligned bounding box (min/max Vec2), with contains() and
  expand() methods.

### Image Types

| Type | Pixel Format | Storage | Notes |
|------|-------------|---------|-------|
| `Bitmap` | uint8_t grayscale | `vector<uint8_t>` | Has `pixel_size_mm`, `aabb()` method |
| `FloatImage` | float grayscale | `vector<float>` | Same layout as Bitmap but 32-bit |
| `ColorImage` | RGB uint8_t | `vector<uint8_t>` (3x) | Interleaved RGB, has `pixel_size_mm` |

All images are row-major, top-to-bottom. Size in mm = width * pixel_size_mm.

### PathSet

- **Path**: `vector<Vec2>` points + `bool closed` + `Color color`
- **PathSet**: `vector<Path>` + AABB (computed on demand via `computeAABB()`)

Paths represent polylines in millimeter coordinates. Closed paths have their
last point implicitly connected to their first.

### Entity (`entity.h`)

The central document object combining:

```
Entity {
    int id
    string name
    LayerPtr payload           // base data (Bitmap/PathSet/etc.)
    unique_ptr<GeneratorBase>  // optional: produces payload from params
    FilterChain filterChain    // transforms payload -> display output
    Mat3 localToPage           // position on A3 canvas
    bool visible
    Color color
}
```

Entity lifecycle per frame:
1. `tickGenerator()` - re-runs generator if params changed (sync or async)
2. `pollAsyncGenerator()` - collects async results when ready
3. `filterChain.output()` - triggers async filter evaluation if stale
4. Renderer draws the filter chain output

---

## 3. The Layer Type System

### LayerKind and ILayerData

All data flowing through filters uses polymorphic `LayerPtr = shared_ptr<ILayerData>`:

```cpp
struct ILayerData {
    virtual ~ILayerData() = default;
    virtual LayerKind kind() const = 0;
};
```

Concrete types (`Bitmap`, `FloatImage`, `ColorImage`, `PathSet`) inherit from
`ILayerData`. Runtime type is checked via `kind()` enum and cast helpers:
- `isBitmapLayer(p)`, `asBitmapPtr(p)`, `asBitmapConstPtr(p)`, etc.
- Template helpers: `ensure<T>(p)` (allocate if wrong type), `as<T>(p)`,
  `asConst<T>(p)` (static_cast with assert)

### Type Flow Through Filters

Filters are typed at compile time (`FilterTyped<InT, OutT>`) but chained at
runtime. The `FilterChain` validates type compatibility:

```
ColorImage -> [ChannelMixer: Color->Bitmap] -> [Blur: Bitmap->Bitmap]
           -> [LineHatch: Bitmap->PathSet] -> [Simplify: PathSet->PathSet]
```

Each filter's `inputKind()` must match the previous filter's `outputKind()`
(or the base layer kind for the first filter). This is checked on add, enable,
disable, and remove operations.

---

## 4. Filter Pipeline

### FilterBase (`src/filters/Filter.h`)

Abstract base for all filters:
- `name()` - display name for UI/serialization
- `inputKind()` / `outputKind()` - type metadata
- `paramVersion()` - monotonic counter for cache invalidation
- `apply(in, out)` - execute the filter (const method)
- `m_parameters` - map of `FilterParameter` structs

### FilterParameter

```cpp
struct FilterParameter {
    enum Type { Float, Int, Bool, Enum, Precise };
    string name;
    float minValue, maxValue, value;
    Type type;                          // controls UI rendering
    vector<string> enumLabels;          // for Enum type
};
```

All values are stored as `float`. The `Type` enum is a UI hint:
- `Float`: continuous slider
- `Int`: integer-snapped slider
- `Bool`: checkbox (0.0 or 1.0)
- `Enum`: dropdown with labels
- `Precise`: high-precision slider (e.g., GPS coordinates)

### FilterTyped<InT, OutT>

Template that handles the polymorphic -> concrete type conversion:

```cpp
void apply(const LayerPtr &in, LayerPtr &out) const override {
    const InT &src = asConst<InT>(in);
    ensure<OutT>(out);
    OutT &dst = as<OutT>(out);
    applyTyped(src, dst);       // subclass implements this
}
```

Concrete filters only need to implement `applyTyped(const InT&, OutT&) const`.

### FilterChain (`src/filters/FilterChain.h`)

Manages an ordered list of filters with:

**Caching**: Each filter slot has a `LayerCache`:
```cpp
struct LayerCache {
    LayerPtr data;
    uint64_t upstreamGen;   // generation of input when cached
    uint64_t paramVer;      // paramVersion() when cached
    uint64_t gen;           // local generation counter
    bool valid;
};
```

A cache entry is stale when `upstreamGen` or `paramVer` don't match current values.
Staleness propagates downstream: changing filter 2's params invalidates filters
2, 3, 4, ... but not filter 1.

**Async evaluation**: `output()` is non-blocking. If caches are stale, it launches
`std::async` to evaluate the chain in a background thread. The last completed
result is returned until the new evaluation finishes. `outputBlocking()` waits
for the current evaluation.

**Type safety**: `addFilter()` asserts type compatibility. `canRemoveFilterAtIndex()`,
`canEnableFilterAtIndex()`, `canDisableFilterAtIndex()` check that toggling a
filter won't break the type chain.

**Enable/disable**: Disabled filters pass upstream data through unchanged, preserving
the type chain as long as `canDisableFilterAtIndex()` permits.

### FilterRegistry (`src/filters/FilterRegistry.h`)

Singleton factory. `initDefaults()` registers all built-in filters. Used by:
- UI: `byInput(kind)` to show compatible filters for the current layer type
- Serialization: lookup by name to reconstruct filters from saved state

---

## 5. Generator System

### GeneratorBase (`src/generators/GeneratorBase.h`)

Generators produce layer data from parameters alone (no input layer). Two models:

**Synchronous** (default): `generate(out)` called on the render thread.
Suitable for cheap operations (procedural shapes, gradients).

**Asynchronous** (`isAsync() == true`): For expensive work (network fetch, file I/O):
1. `startGenerate()` kicks off a background thread
2. `isReady()` polled each frame (non-blocking)
3. `collectResult(out)` transfers result to render thread

Generators share the `FilterParameter` system with filters, plus a separate
`m_stringParameters` map for text/file path inputs.

### GeneratorTyped<Derived, OutT>

CRTP template that handles type conversion and provides a default `clone()`
that copies all float and string parameters.

### GeneratorRegistry

Same pattern as FilterRegistry: singleton factory, `initDefaults()` registers
built-in generators.

### Generator-Entity Integration

In `Entity::tickGenerator()`:
- Compares `generator->paramVersion()` against last-seen version
- For sync generators: calls `generate()` immediately, updates payload
- For async generators: calls `startGenerate()`, result collected in
  `pollAsyncGenerator()` on a subsequent frame
- When payload changes, `refreshFilterBase()` propagates to the FilterChain

---

## 6. Rendering

### Architecture

OpenGL 3.3 Core Profile. All GLSL shaders are embedded as C++ string literals.

```
Renderer (composite facade)
  ├── LineRenderer    - PathSet -> GL_LINES + GL_POINTS
  ├── BitmapRenderer  - Bitmap/ColorImage -> textured quads
  └── FloatImageRenderer - FloatImage -> value-mapped quads
```

### Camera (`src/Camera.h`)

Orthographic projection mapping mm coordinates to NDC [-1, 1]:
- `zoom`: vertical half-extent in mm
- `screenToWorld(px)`: pixel coordinates -> mm coordinates
- `Transform()`: returns the mm-to-NDC Mat3

### Renderer (`src/Renderer.h`)

Dispatches per-entity rendering based on the filter chain output type:
- PathSet -> LineRenderer (with optional node visualization)
- Bitmap/ColorImage -> BitmapRenderer
- FloatImage -> FloatImageRenderer

Split rendering mode (`beginFrame` / `endFrame`) allows injecting overlay
geometry (e.g., plot progress lines) between scene setup and draw.

### LineRenderer

Builds vertex arrays from PathSet data. Features:
- GL_LINES for path segments
- GL_POINTS with point sprites for path nodes
- Per-vertex color
- Configurable line width and node diameter

### BitmapRenderer

Uploads image data as GL textures (GL_R8 for grayscale, GL_RGB8 for color).
Renders as a textured quad positioned by the entity transform.

---

## 7. Plotter Pipeline

### Stack

```
PlotterManager (facade)
  -> PlotSpooler (background thread, path ordering, command buffering)
    -> MotionPlanner (trapezoidal velocity profiles, junction deviation)
      -> AxidrawController (EBB protocol: SM, SP, SC commands)
        -> SerialController (platform USB serial I/O)
```

### PlotterManager (`src/plotters/PlotterManager.h`)

High-level facade providing:
- `connect(port)` / `disconnect()`
- `startJob(page)` / `startJobSingle(page, entityId)`
- `pause()` / `resume()` / `cancel()`
- `penUp()` / `penDown()` / `disengageMotors()`
- Status queries and ImGui panel rendering

### PlotSpooler (`src/plotters/PlotSpooler.h`)

Runs a background worker thread that:
1. Collects PathSet data from visible entities (via `outputBlocking()`)
2. Reorders paths for efficient plotting (nearest-neighbor or Hilbert curve)
3. Plans motion via MotionPlanner
4. Streams MoveSlice commands to AxidrawController
5. Maintains high/low watermarks (~1200ms / ~300ms) for command buffering
6. Reports real-time progress (paths completed, time elapsed)

### MotionPlanner (`src/plotters/MotionPlanner.h`)

Converts mm-coordinate paths to stepper motor commands:
- Trapezoidal velocity profiles (accel -> cruise -> decel)
- Junction deviation algorithm for smooth cornering
- CoreXY kinematics: A = dx + dy, B = dx - dy
- Outputs `MoveSlice` structs: (aSteps, bSteps, dtMs) per time slice
- Respects step rate limits and configurable acceleration/velocity

### AxidrawController (`src/plotters/AxidrawController.h`)

Translates high-level commands to EBB (EiBotBoard) serial protocol:
- `SM` (stepper move), `SP` (set pen), `SC` (set configuration)
- Pen up/down servo positions with configurable timing
- Motor enable/disable

### SerialController (`src/serial/SerialController.h`)

Cross-platform USB serial I/O:
- Windows: CreateFile/ReadFile/WriteFile
- macOS: open/read/write on `/dev/cu.*`
- Linux: open/read/write on `/dev/ttyUSB*` or `/dev/ttyACM*`

---

## 8. Interaction System

### InteractionController (`src/Interaction.h`)

Handles mouse input and entity manipulation:

**Modes**:
- `None` - idle
- `PanningCamera` - middle-click or right-click drag
- `DraggingEntity` - left-click drag on entity body
- `ResizingEntity` - left-click drag on resize handles (N/S/E/W/NE/NW/SE/SW)

**Selection model**: Single selection. `activeId` is the selected entity.
`hoveredId` is the entity under the cursor. Both are `optional<int>`.

**Hit testing**: Entity bounds are computed via `Entity::boundsLocal()` which
dispatches by payload type (PathSet AABB, Bitmap aabb from dimensions).

---

## 9. Serialization

### Format

JSON via nlohmann-json. File structure (`page.json`):

```json
{
    "entities": [
        {
            "id": 1, "name": "...", "type": "Bitmap",
            "transform": [9 floats],
            "visible": true, "color": [4 floats],
            "payload": { ... type-specific data ... },
            "generator": { "type": "Circle", "params": {...}, "stringParams": {...} },
            "filters": [
                { "name": "Blur", "enabled": true, "params": {"radius": 2.0} }
            ]
        }
    ],
    "camera": { "centerX": ..., "centerY": ..., "zoom": ... },
    "plotter": { ... PlotterConfig fields ... }
}
```

### Bitmap compression

Bitmap pixel data is stored as RLE + Base64:
1. RLE encode: run-length pairs (count, value)
2. Base64 encode the RLE bytes
3. Store as a JSON string

FloatImage data uses Base64 only (no RLE).

### Filter/Generator reconstruction

Filters and generators are looked up by name in their respective registries.
Parameters are restored via `setParameter()` / `setStringParameter()`.

---

## 10. Build System

### CMake

- C++17 standard required
- vcpkg manifest mode for dependency management
- `GLOB_RECURSE` for source files (auto-discovers new `.cpp` files, but requires
  CMake re-run)
- Three test targets: `minotaur_tests`, `motion_planner_tests`, `filter_tests`
- `BUILD_APP` option to skip the main executable (for CI test-only builds)

### Platform specifics

- macOS: OBJCXX language for Objective-C++ files, macOS bundle with icon
- Windows: Resource file for icon, links windowscodecs for WIC image loading
- Linux: Standard OpenGL linkage

### Dependencies

| Package | Purpose |
|---------|---------|
| glfw3 | Window/input management |
| glad | OpenGL loader |
| imgui | Immediate-mode GUI (with GLFW+OpenGL3 backends) |
| fmt | String formatting |
| glog | Logging |
| nlohmann-json | JSON serialization |
| gtest | Unit testing |
| curl | HTTP requests (OSM tile fetching) |
