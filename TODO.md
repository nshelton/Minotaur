# Minotaur TODO

## Architecture Refactors

These are ordered by impact and effort. Each section contains enough detail for an
agent to pick up the task and implement it without additional context.

---

### R1. Richer FilterParameter Types
**Effort:** Low | **Impact:** High | **Files:** 3-4

The `FilterParameter` struct in `src/filters/Filter.h` only supports `float`. Every
filter that needs a boolean, integer, or enum must encode it as a float and decode it
manually (`value > 0.5f` for bools, `static_cast<int>(std::lround(...))` for ints).
The UI in `MainScreen.gui.cpp` renders everything as `ImGui::SliderFloat`.

**What to do:**

1. In `src/filters/Filter.h`, add a `Type` enum to `FilterParameter`:
   ```cpp
   struct FilterParameter {
       enum Type { Float, Int, Bool, Enum } type{Float};
       std::string name;
       float minValue{0.0f}, maxValue{1.0f}, value{0.5f};
       std::vector<std::string> enumLabels;  // only used when type == Enum
   };
   ```
   Keep the default `Float` so all existing filters continue to work unchanged.

2. In `src/screens/MainScreen.gui.cpp` (~line 468), replace the single `SliderFloat`
   loop with a switch on `param.type`:
   - `Float` -> `ImGui::SliderFloat` (existing behavior)
   - `Int` -> `ImGui::SliderInt` (cast value to int, write back as float)
   - `Bool` -> `ImGui::Checkbox` (value > 0.5 = checked)
   - `Enum` -> `ImGui::Combo` with `enumLabels` as items

3. In `src/utils/Serialization.cpp`, serialize the `type` field alongside the value.
   For backward compat, if `type` is missing during load, default to `Float`.

4. Migrate a few existing filters as proof:
   - `LevelsFilter`: change `"invert"` param to `Bool`
   - `ErodeDilateFilter`: change `"operation"` param to `Enum` with labels `{"Erode", "Dilate"}`
   - `BlurFilter`: change `"radius"` param to `Int`
   - Leave the cast logic in `applyTyped()` as-is for now; it still works with float storage

**Verify:** Build and run. Open an entity with a LevelsFilter - `invert` should render
as a checkbox. Open ErodeDilate - `operation` should render as a dropdown. Existing
`page.json` files should still load correctly.

---

### R2. Make FilterChain::output() const
**Effort:** Low | **Impact:** High | **Files:** 2

`FilterChain::output()` in `src/filters/FilterChain.h` is non-const because
`evaluate()` updates cache members. This forces `const_cast<Entity&>` in
`src/Renderer.cpp` (lines ~30 and ~131).

**What to do:**

1. In `src/filters/FilterChain.h`, mark all cache-related members `mutable`:
   ```cpp
   mutable std::vector<LayerCache> m_caches;
   mutable uint64_t m_baseGen{0};
   ```
   Also mark any other members that `evaluate()` writes to as `mutable`.

2. Make `evaluate()` and `output()` const:
   ```cpp
   const LayerPtr &output() const { ... }
   const LayerPtr &evaluate(size_t idx) const { ... }
   ```

3. In `src/Renderer.cpp`, remove both `const_cast<Entity&>` calls. Access
   `entity.filterChain.output()` directly on the const Entity reference.

**Verify:** Build. Grep for `const_cast` in Renderer.cpp - should be zero hits.
Run the app, confirm rendering still works (caching still invalidates correctly).

---

### R3. Consistent PageModel::addEntity<T>() Template
**Effort:** Low | **Impact:** Medium | **Files:** 2

`src/Page.h` and `src/Page.cpp` have three near-identical methods: `addPathSet()`
returns `int`, `addBitmap()` returns `void`, `addColorImage()` returns `void`.

**What to do:**

1. In `src/Page.h`, replace the three declarations with one template:
   ```cpp
   template <typename T>
   int addEntity(const T &data, const std::string &name = "");
   ```

2. In `src/Page.cpp`, replace the three implementations with:
   ```cpp
   template <typename T>
   int PageModel::addEntity(const T &data, const std::string &name) {
       int id = entities.empty() ? 0 : entities.rbegin()->first + 1;
       Entity e;
       e.id = id;
       e.name = name.empty() ? "Entity " + std::to_string(id) : name;
       e.payload = data;
       e.localToPage = Mat3();
       e.refreshFilterBase();
       entities[id] = e;
       return id;
   }
   // Explicit instantiations:
   template int PageModel::addEntity<PathSet>(const PathSet&, const std::string&);
   template int PageModel::addEntity<Bitmap>(const Bitmap&, const std::string&);
   template int PageModel::addEntity<ColorImage>(const ColorImage&, const std::string&);
   template int PageModel::addEntity<FloatImage>(const FloatImage&, const std::string&);
   ```

3. Update all callers (grep for `addPathSet`, `addBitmap`, `addColorImage`):
   - `MainScreen.cpp` and `MainScreen.gui.cpp` - replace calls
   - `Serialization.cpp` - replace calls
   - Keep `duplicateEntity()` as-is (different pattern)

**Verify:** Build. Run. Drop an image into the app - should create an entity. Save
and reload page.json.

---

### R4. Extract PlotterManager from MainScreen
**Effort:** Medium | **Impact:** High | **Files:** 4-5 new/modified

`MainScreen.h` directly owns SerialController, AxiDrawController, AxiDrawState,
PlotSpooler, and PlotterConfig. The GUI code in `MainScreen.gui.cpp` manipulates
all of these across ~100 lines.

**What to do:**

1. Create `src/plotters/PlotterManager.h` and `.cpp` containing:
   ```cpp
   class PlotterManager {
   public:
       void connect(const std::string &port);
       void disconnect();
       bool isConnected() const;

       void startJob(const PageModel &page);
       void pause();
       void resume();
       void cancel();

       void penUp();
       void penDown();

       PlotterConfig &config();
       const PlotSpooler::Stats &stats() const;
       PathSet getRemainingPaths() const;

       void renderGui();  // ImGui panel for plotter controls

   private:
       SerialController m_serial;
       std::unique_ptr<AxiDrawController> m_ax;
       std::unique_ptr<PlotSpooler> m_spooler;
       PlotterConfig m_config;
       AxiDrawState m_axState;
   };
   ```

2. Move all plotter-related GUI code from `MainScreen.gui.cpp` into
   `PlotterManager::renderGui()`.

3. In `MainScreen.h`, replace the 5 plotter fields with:
   ```cpp
   PlotterManager m_plotter;
   ```

4. In `MainScreen.cpp`, replace direct plotter manipulation with calls to
   `m_plotter.connect()`, `m_plotter.startJob(m_page)`, etc.

5. Update `Serialization.cpp` to save/load `PlotterConfig` via
   `m_plotter.config()` instead of a separate field.

**Verify:** Build and run. Connect to an AxiDraw (or just verify the serial port
dropdown still appears). Start/pause/cancel a plot. Save and reload project.

---

### R5. Remove Mouse State from PageModel
**Effort:** Low | **Impact:** Medium | **Files:** 3-4

`mouse_pixel` and `mouse_page_mm` in `src/Page.h` are transient UI state stored in
the document model. They are written by MainScreen input handlers and read by
Interaction and Renderer.

**What to do:**

1. Remove `mouse_pixel` and `mouse_page_mm` from `PageModel` in `src/Page.h`.

2. Add them to `InteractionState` in `src/Interaction.h` (which already exists and
   is passed alongside PageModel in render calls).

3. Update all readers/writers:
   - `MainScreen.cpp` cursor callback: write to `m_interactionState` instead of `m_page`
   - `Renderer.cpp`: read from InteractionState instead of PageModel
   - `Interaction.cpp`: read from InteractionState (already has access)
   - Grep for `mouse_pixel` and `mouse_page_mm` to catch any others.

**Verify:** Build and run. Hover over entities - selection highlights should still work.
Pan/zoom should still work. Save project - verify mouse coords are NOT in the JSON.

---

### R6. Unify the Dual Type Systems
**Effort:** Medium | **Impact:** High | **Files:** 5-6

The codebase has two parallel type dispatch mechanisms:
- `ILayerData::kind()` with helpers in `src/filters/Types.h` (used by filters/renderer)
- `std::variant<PathSet, Bitmap, FloatImage, ColorImage>` in `src/core/entity.h`
  (used by entity/page)

These can disagree and require duplicate dispatch logic.

**What to do:**

1. Change Entity's payload from `std::variant` to `LayerPtr` (which is
   `shared_ptr<ILayerData>` and already carries `LayerKind`):
   ```cpp
   struct Entity {
       LayerPtr payload;  // was: std::variant<PathSet, Bitmap, FloatImage, ColorImage>
       // ...
   };
   ```

2. Rewrite `Entity::boundsLocal()`, `Entity::type()`, `Entity::baseLayer()` etc. to
   use `payload->kind()` and the `as<T>()` / `asConst<T>()` helpers from `Filter.h`
   instead of `std::get_if`.

3. Update `PageModel::addEntity<T>()` to wrap the data in a `shared_ptr`:
   ```cpp
   e.payload = std::make_shared<T>(data);
   ```

4. Update `Serialization.cpp` to serialize/deserialize via `LayerKind` dispatch
   instead of variant visitors.

5. Update `MainScreen.gui.cpp` entity display code that currently uses `std::get_if`
   or `std::holds_alternative`.

6. Remove the variant typedef and any `std::get_if` / `std::holds_alternative` calls.

**Verify:** Build and run. Load an existing page.json. Add new entities of each type
(drop image, create path). Save and reload. All entity operations should work.

---

### R7. Filter Unit Tests
**Effort:** Medium | **Impact:** High | **Files:** 2-3 new

Filters are pure functions with zero global state - ideal for testing. Yet none have
tests. This is the most important thing for safe agent contributions.

**What to do:**

1. Create `tests/test_filters.cpp` with a GTest suite. Add to CMakeLists.txt:
   ```cmake
   add_executable(filter_tests
     tests/test_filters.cpp
     # Include all filter .cpp files (or link the relevant ones)
   )
   target_include_directories(filter_tests PRIVATE src)
   target_link_libraries(filter_tests PRIVATE GTest::gtest GTest::gtest_main)
   add_test(NAME filter_tests COMMAND filter_tests)
   ```
   Note: you'll need to figure out which .cpp files to include. Filters that are
   header-only just need the header. Filters with .cpp files need those compiled in.

2. Write test helpers to create small fixtures:
   ```cpp
   Bitmap makeCheckerboard(int w, int h, int blockSize);
   Bitmap makeSolidBitmap(int w, int h, uint8_t value);
   PathSet makeSquarePath(float side);
   PathSet makeCirclePath(float radius, int segments);
   ```

3. Write tests for at least these filters:
   - **BlurFilter**: solid white input -> output still solid white. Checkerboard ->
     output has lower contrast. Radius 0 -> identity.
   - **ThresholdFilter**: known input -> expected binary output.
   - **SimplifyFilter**: straight line -> reduced to 2 points. Circle with high
     tolerance -> fewer points. Tolerance 0 -> identity.
   - **TraceFilter**: solid rectangle bitmap -> produces closed path.
   - **LevelsFilter**: identity settings -> output matches input.

4. Each test should: create input, create filter, set parameters, call
   `filter.applyTyped(in, out)`, verify output properties.

**Verify:** `cmake --build build --target filter_tests && ctest -R filter_tests`

---

### R8. Inject Project File Path
**Effort:** Low | **Impact:** Low | **Files:** 2

`"page.json"` is hardcoded in `MainScreen.cpp` (lines ~21 and ~112 in save/load).

**What to do:**

1. Add a constructor parameter to MainScreen:
   ```cpp
   explicit MainScreen(const std::string &projectPath = "page.json");
   ```
   Store as `m_projectPath`.

2. Replace both hardcoded `"page.json"` strings with `m_projectPath`.

3. Optionally, accept a command-line argument in `main.cpp`:
   ```cpp
   int main(int argc, char *argv[]) {
       std::string projectPath = (argc > 1) ? argv[1] : "page.json";
       MainScreen screen(projectPath);
       // ...
   }
   ```

**Verify:** Build. Run with `./minotaur myproject.json`. Save and reload - should
use the specified filename.

---

### R9. Decouple Serialization from Renderer
**Effort:** Low | **Impact:** Medium | **Files:** 2

`saveProject()` and `loadProject()` in `src/utils/Serialization.h` take `Renderer&`
to persist line width and node diameter. This couples serialization to rendering.

**What to do:**

1. Create a small POD struct in `src/utils/Serialization.h`:
   ```cpp
   struct RenderSettings {
       float lineWidth{1.0f};
       float nodeDiameter{3.0f};
       // add other persisted render settings here
   };
   ```

2. Change the serialization signatures:
   ```cpp
   bool saveProject(..., const RenderSettings &render, ...);
   bool loadProject(..., RenderSettings &render, ...);
   ```

3. In MainScreen, convert to/from Renderer at the call site:
   ```cpp
   RenderSettings rs{m_renderer.lineWidth(), m_renderer.nodeDiameter()};
   serialization::saveProject(m_page, m_camera, rs, m_plotter, path);
   ```

4. Remove `#include "Renderer.h"` from Serialization.cpp.

**Verify:** Build. Save and load a project. Line width and node size should persist.

---

## Feature Backlog

### Rendering / UI
- [ ] Draw bitmap and paths together when a bitmap entity has path-producing filters
  (per-layer visibility, different colors per layer)
- [ ] Centerline trace (currently hangs)
- [ ] Live plot progress overlay - change line color blue->orange as paths are plotted

### Performance
- [ ] Use KdTree for path optimization
- [ ] Audit memory usage in filter chain

### Filters
- [ ] Line displace filter (needs winding mode for closed paths to determine normal direction)
- [ ] Blur has artifacts if radius < 1

### New Layer Types
- [ ] "Field" layer type for scalar/vector fields from raster or paths
- [ ] Field-based line extraction filters
- [ ] Quiver plot renderer for vector fields

## Bugs
- [ ] Reproducible offset bug during plotting: at some point an offset is introduced,
  causing all subsequent moves to be misaligned by a few cm. Likely related to the
  junction deviation formula in MotionPlanner (see MOTION_PLANNER_ANALYSIS.md).
