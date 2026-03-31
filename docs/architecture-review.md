# Architecture Review: Tradeoffs, Issues, and Improvement Proposals

This document analyzes the current Minotaur architecture, identifies design
tradeoffs, and proposes improvements for modularity, portability, maintainability,
extensibility, and readability.

---

## Current Strengths

1. **Plugin-like extensibility**: Adding filters/generators requires only a new
   class + one registry line. No UI wiring, no serialization code, no CMake changes.
   This is excellent.

2. **Type-safe pipeline**: Compile-time typing via `FilterTyped<In,Out>` with
   runtime validation in FilterChain prevents type mismatches at both levels.

3. **Async-ready evaluation**: FilterChain's `std::async` background evaluation
   and Generator's sync/async split keep the UI responsive during heavy computation.

4. **Clean separation of concerns**: Core types have zero dependencies. Filters
   don't know about rendering. The plotter stack is independent of the UI.

5. **Simple parameter system**: Single `FilterParameter` type with float values
   handles all UI needs without complexity.

---

## Issues and Improvement Proposals

### 1. FilterChain Thread Safety

**Issue**: `FilterChain::evaluate()` and `output()` use `mutable` fields and
`std::async` but have subtle races:
- `m_layers` is mutated by the background thread while `needsEval()` reads it
  on the main thread
- `m_evalFuture.valid()` and `.wait_for()` are called without locks
- Multiple rapid parameter changes can queue evaluations that overlap

**Risk**: Potential data races, especially with fast parameter slider dragging.

**Proposal**: Replace the bare `std::async` + mutable state pattern with an
explicit worker model:
- Single dedicated evaluation thread (not a new thread per eval)
- Atomic flag for "evaluation requested" / "evaluation running"
- Double-buffer the result: worker writes to a staging buffer, main thread
  swaps it in atomically
- Cancel-and-restart semantics for parameter changes during evaluation

**Priority**: High (correctness)

---

### 2. File Organization: Misplaced Headers

**Issue**: Several files are in unexpected locations:
- `Renderer.h/cpp` and `Camera.h/cpp` are in `src/` instead of `src/render/`
- `entity.h` is in `src/core/` but depends on `FilterChain` and `GeneratorBase`,
  making it not a pure value type like the other core types
- `Page.h/cpp` and `Interaction.h/cpp` are in `src/` root

**Proposal**:
- Move `Renderer.*` and `Camera.*` into `src/render/`
- Move `entity.h` to `src/` (it's a composite, not a core value type)
- Create `src/scene/` for `Page.*`, `Interaction.*`, and `entity.h` to group
  document-model code together
- Or simply move everything in `src/` root into logical subdirectories

**Priority**: Medium (readability, discoverability)

---

### 3. Entity is a God Object

**Issue**: `Entity` in `entity.h` combines:
- Data ownership (payload, generator)
- Pipeline management (FilterChain, tickGenerator, pollAsyncGenerator)
- Geometry (localToPage transform, boundsLocal, contains)
- Display properties (visible, color, name)
- Type casting convenience (pathset(), bitmap(), etc.)

This makes it hard to reason about responsibilities and test in isolation.

**Proposal**: Consider splitting into composable parts:
- `EntityData` - payload, generator, filter chain (the processing pipeline)
- `EntityTransform` - localToPage, boundsLocal, hit testing
- `EntityDisplay` - visible, color, name
- Or at minimum, extract the generator tick logic into a free function

**Priority**: Low-medium (the current struct works, but becomes harder to
extend as features are added)

---

### 4. Singleton Registries

**Issue**: `FilterRegistry` and `GeneratorRegistry` are singletons with
`static bool initialized` guards. This creates:
- Hidden global state
- Initialization order dependencies (must call `initDefaults()` before
  any deserialization)
- Difficulty testing with custom filter sets
- No way to have multiple independent registry instances

**Proposal**: Pass registries by reference rather than using singletons:
- `App` or `MainScreen` owns the registries
- Serialization functions take a `const FilterRegistry&` parameter
- UI code receives registry references
- `initDefaults()` becomes a free function that populates a registry

This makes dependencies explicit and enables testing with mock registries.

**Priority**: Medium (testability, clarity)

---

### 5. FilterParameter Limitations

**Issue**: All parameter values are `float`, with `Type` as a UI hint only.
This leads to:
- Bool parameters stored as 0.0/1.0 and cast to bool in filters
- Int parameters requiring manual `static_cast<int>()` everywhere
- No string parameters on filters (only generators have them)
- No compound parameter types (Vec2, color picker, etc.)
- No parameter grouping or conditional visibility

**Proposal** (incremental):
1. **Short term**: Add a `stringValue` field to FilterParameter for filters that
   need text input (currently only generators have `m_stringParameters`)
2. **Medium term**: Add typed accessors: `paramInt(key)`, `paramBool(key)`,
   `paramFloat(key)` that handle the casting
3. **Long term**: Consider a variant-based parameter value, or keep float but
   add metadata for parameter groups, dependencies, and tooltips

**Priority**: Low (current system works but is somewhat awkward)

---

### 6. FilterChain Owns Too Many Concerns

**Issue**: `FilterChain` manages:
- Filter ownership and ordering
- Type validation
- Cache invalidation
- Async evaluation and threading
- Result double-buffering
- Statistics tracking

This makes it hard to change any one concern without affecting others.

**Proposal**: Separate into:
- `FilterPipeline` - filter list, type validation, add/remove/enable/disable
- `PipelineCache` - LayerCache management, generation tracking, invalidation
- `PipelineEvaluator` - async execution, result buffering, progress

**Priority**: Medium (maintainability)

---

### 7. Lack of Cancellation for Filter Evaluation

**Issue**: Once a filter evaluation starts, it runs to completion. For expensive
filters (FlowSnake with 500k steps, Voronoi with 100k points), changing
parameters means waiting for the old evaluation to finish before the new one
starts.

**Proposal**: Add a cancellation token:
- `FilterBase::apply()` receives a `const std::atomic<bool>& cancelled` parameter
- Expensive filters check the token periodically and return early
- FilterChain sets the token when it needs to restart evaluation
- This integrates naturally with the existing `setProgress()` mechanism

**Priority**: Medium (UX improvement for expensive filters)

---

### 8. No Undo/Redo

**Issue**: There is no undo system. Parameter changes, entity additions/removals,
and transform changes are all permanent.

**Proposal**: Command pattern with an undo stack:
- Each user action creates a `Command` object with `execute()` and `undo()`
- Commands store the minimal state delta needed to reverse
- For parameter changes: store (entityId, filterIndex, paramKey, oldValue, newValue)
- For entity operations: store the full entity state
- Stack depth limit to control memory usage

**Priority**: Medium-high (major UX gap)

---

### 9. Monolithic MainScreen

**Issue**: `MainScreen` owns all application state and coordinates all subsystems.
The GUI code alone is a separate TU due to size. This makes it hard to:
- Test subsystems independently
- Add new panels or screens
- Reason about state flow

**Proposal**: Extract subsystem controllers:
- `EntityPanel` - entity list, selection, properties GUI
- `FilterPanel` - filter chain GUI, add/remove/reorder
- `GeneratorPanel` - generator properties GUI
- `ViewportController` - handles rendering, camera, overlay drawing
- `PlotterManager` already exists as a partial extraction

Each controller receives references to shared state (PageModel,
InteractionState, Camera) rather than owning everything.

**Priority**: Medium (maintainability, but the split TU approach works for now)

---

### 10. Test Coverage

**Issue**: Tests cover only:
- KdTree2D (spatial indexing utility)
- MotionPlanner (28 cases - good coverage)
- 5 basic filters (Blur, Threshold, Levels, Trace, Simplify)

Missing coverage:
- FilterChain logic (caching, type validation, async eval, enable/disable)
- Generator framework (sync/async lifecycle, parameter versioning)
- Serialization round-trips
- Complex filters (FlowSnake, VoronoiStippling, etc.)
- Entity lifecycle (generator tick, filter chain integration)

**Proposal**: Priority test additions:
1. FilterChain unit tests (most critical - threading + caching logic)
2. Serialization round-trip tests (save entity -> load -> compare)
3. Integration test: generator -> filter chain -> output type validation
4. Parameterized filter tests: verify each filter produces non-empty output

**Priority**: High (confidence in correctness, especially for FilterChain threading)

---

### 11. Memory Copies in Filter Pipeline

**Issue**: `Entity::baseLayer()` makes a full copy of the payload for the filter
chain. For large bitmaps (e.g., 4000x4000 = 16MB), this is significant. Each
filter also gets its own output allocation via `ensure<T>()`.

**Proposal**:
- Use `shared_ptr` with copy-on-write semantics for the base layer
- Filters that don't modify data (pass-through when disabled) already share
  pointers, but the initial copy could be avoided
- Consider an arena allocator for filter chain intermediates to reduce
  allocation overhead

**Priority**: Low (only matters for very large images)

---

### 12. Hardcoded Page Size

**Issue**: Page dimensions are hardcoded to A3 (297x420mm) as `const float`
members in `PageModel`. No way to change paper size at runtime.

**Proposal**: Make page size configurable:
- Add width/height parameters to PageModel
- Support common paper sizes (A4, A3, Letter, custom)
- Save/load page dimensions with the project
- Update Camera reset to center on the configured page

**Priority**: Low (A3 is the target use case, but configurability is nice)

---

### 13. Header-Only Complexity

**Issue**: Several complex implementations are header-only:
- `MvtDecoder.h` - protobuf parsing (~300+ lines)
- `HttpFetcher.h` - curl wrapper
- `ObjLoader.h` - OBJ file parsing
- `MeshDecimator.h` - QEM mesh decimation

This increases compile times for every TU that includes them and makes
dependency tracking harder.

**Proposal**: Move implementations to `.cpp` files, keeping only the interface
in headers. Since CMake uses `GLOB_RECURSE`, adding new `.cpp` files just
requires a CMake re-run.

**Priority**: Low (affects compile time, not correctness)

---

### 14. Portable Serial/Platform Code

**Issue**: `SerialController.cpp` and `ImageLoader.cpp` use `#ifdef` blocks
for platform-specific code within single files. `Clipboard.h` has a
platform-specific `.mm` (Objective-C++) implementation for macOS.

**Proposal**: Use platform-specific source files instead of ifdefs:
- `SerialController_win.cpp`, `SerialController_mac.cpp`, `SerialController_linux.cpp`
- CMake selects the right file per platform
- Common interface in the header remains unchanged
- Easier to read, test, and add new platform support

**Priority**: Low (the ifdef approach works, but gets harder to maintain)

---

## Summary: Recommended Priority Order

| # | Proposal | Impact | Effort | Priority |
|---|----------|--------|--------|----------|
| 1 | FilterChain thread safety | Correctness | Medium | **High** |
| 10 | Test coverage (FilterChain, serialization) | Confidence | Medium | **High** |
| 8 | Undo/redo | UX | High | **Medium-High** |
| 7 | Filter cancellation tokens | UX | Low | **Medium** |
| 4 | Non-singleton registries | Testability | Medium | **Medium** |
| 6 | Split FilterChain concerns | Maintainability | Medium | **Medium** |
| 9 | Extract MainScreen subsystems | Maintainability | High | **Medium** |
| 2 | File reorganization | Readability | Low | **Medium** |
| 3 | Split Entity responsibilities | Design | Medium | **Low-Medium** |
| 5 | Typed parameter accessors | Ergonomics | Low | **Low** |
| 11 | Reduce pipeline memory copies | Performance | Medium | **Low** |
| 12 | Configurable page size | Features | Low | **Low** |
| 13 | Move header-only impls to .cpp | Compile time | Low | **Low** |
| 14 | Platform-specific source files | Portability | Medium | **Low** |
