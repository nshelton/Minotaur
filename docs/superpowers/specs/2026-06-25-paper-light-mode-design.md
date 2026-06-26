# Paper / Light Mode — Design

**Date:** 2026-06-25
**Status:** Approved (design)

## Goal

Add a "paper mode" (a.k.a. light mode) so the user can preview what a plot will
actually look like on physical paper: a white page on a desk, with pen strokes
rendered as real ink — correct color, correct physical width, and overlaps that
darken the way ink does. It is a view toggle; it does not change the underlying
geometry, filters, or plotting pipeline.

## Why the obvious approach is wrong

The `LineRenderer` currently uses **additive blending** (`glBlendFunc(GL_SRC_ALPHA, GL_ONE)`,
`LineRenderer.cpp:157`). That makes glowing lines on the dark canvas, but on white
paper additive blending *brightens* the paper — a black pen line renders invisible
and overlapping strokes wash out instead of getting darker. So paper mode needs a
different blend model (multiply), not just a different clear color.

Two other current facts shape the design:
- There is **no filled-shape capability** in the renderer — only `GL_LINES` and
  point-sprite circles. The white page fill needs new triangle geometry.
- `glLineWidth` (`LineRenderer.cpp:169`) is clamped to 1px on macOS core profile,
  so real pen width must come from CPU-built quad geometry, not line width state.

The existing vertex format (`pos.xy` + RGBA) and the existing shader already support
everything needed; we add a triangle pass and a blend-mode switch rather than a new
pipeline.

## Decisions (from brainstorming)

| Question | Decision |
|---|---|
| Scope | Canvas paper preview **and** whole UI light (ImGui light theme) |
| Ink blend model | **Subtractive/multiply** — strokes darken paper, overlaps accumulate |
| Pen color source | Reuse per-entity `entity.color`; treat default white `(1,1,1)` as **black** ink |
| Pen width | **Real physical width in mm** (default 0.4mm), scales with zoom |
| Thick-line technique | **CPU quad expansion + point-sprite round caps/joins** (reuses existing point pass) |
| Desk vs all-paper | Medium-gray "desk" with a white page rect sitting on it |

## Architecture

### State & toggle
- `bool m_paperMode` on `MainScreen` (default false / dark).
- Toggled via a View menu item, a toolbar button, and keybind `L`.
- Persisted with the camera/view settings in the project JSON (`Serialization.cpp`),
  so a saved project reopens in the same mode.
- On toggle it drives four things: ImGui style, GL clear color, page fill, and the
  `LineRenderer` blend/width path.

### UI chrome (ImGui)
- Call `ImGui::StyleColorsLight()` in paper mode, `ImGui::StyleColorsDark()` otherwise,
  in the same one-time-style location pattern used in `App.cpp`.
- After applying the light style, darken a couple of slots (borders/text) for
  legibility. The two canonical accent colors in `Theme.h` (path blue, bitmap green)
  are unchanged — they read acceptably on light.

### Desk and page (`Renderer::renderPage`)
- **Clear color:** medium neutral gray (~0.82) in paper mode; current dark charcoal
  `(0.1, 0.1, 0.12)` in dark mode.
- **Page fill:** a filled white quad covering the A3 rect (0,0)-(page_w, page_h).
  Requires a new filled-triangle batch in `LineRenderer` (see below).
- **Grid:** very faint gray (or suppressed inside the page); page outline thin
  dark-gray. Letter-paper guide rects kept, faint.

### Filled-triangle batch (new, in `LineRenderer`)
- Add `std::vector<GLVertex> m_tris;` and `void addQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col);`
  (emits two triangles).
- Draw as `GL_TRIANGLES` reusing the existing program and VBO, with normal alpha
  blend (`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`) for the page fill — independent of
  the ink blend mode. Cleared each frame like `m_points`.

### Ink rendering (core)
In paper mode the `Renderer` builds path geometry differently (dark mode path is
untouched):
- Each path segment → an mm-width quad (width = pen width) via `LineRenderer::addQuad`.
- Each path vertex → a point-sprite circle of pen diameter (reusing the existing
  point pass) for round joins and caps. This matches how a pen physically moves.
- Geometry is in mm/page space, so zoom scaling is automatic via the existing
  `mm_to_ndc` projection.

**Blend mode switch:** `LineRenderer` gets a paper-mode flag. The ink passes (quads +
points) use **multiply** `glBlendFunc(GL_DST_COLOR, GL_ZERO)` in paper mode, and the
current additive blend in dark mode. Under multiply on white paper, one stroke shows
the exact pen color and overlaps multiply toward darker.

**Pen color resolution (pure, testable):**
- Start from `entity.color`.
- If RGB is the default white `(1,1,1)`, substitute black `(0,0,0)`.
- Apply translucency by lerping the resulting color toward white by `(1 - alpha)`,
  so a low-alpha layer reads as lighter ink under multiply.

### Bitmaps & non-path layers
- Rendered as-is (their own pixels) in both modes — no change. They are source /
  intermediate data, not ink.
- Future option (out of scope): dim or hide non-path layers in paper mode.

## Components touched

| File | Change |
|---|---|
| `src/screens/MainScreen.h/.cpp` | `m_paperMode` state, toggle, keybind `L`, pen-width setting |
| `src/screens/MainScreen.gui.cpp` | View menu/toolbar toggle, pen-width control |
| `src/app/App.cpp` | ImGui light/dark style switch, clear-color switch driven by paper mode |
| `src/render/LineRenderer.h/.cpp` | `m_tris` + `addQuad`, blend-mode flag, multiply path |
| `src/Renderer.cpp` | paper-mode branch in `renderPage` (white fill, grid) and in path rendering (quad + cap geometry, pen-color resolution) |
| `src/utils/Serialization.cpp` | persist `paperMode` (+ pen width) with view settings |

## Pen width
- Global setting (not per-layer yet), default **0.4mm**, exposed as a control in the
  view UI. Per-layer pen width is a possible future extension.

## Testing
- Rendering is GL-side and not directly unit-testable, but the pure logic is:
  - **Pen-color resolution:** white→black substitution and alpha→lerp-to-white.
  - **Segment→quad expansion:** given `p1, p2, width` produce the 4 corner points
    (perpendicular offset), and the degenerate zero-length case.
- Add these as a small unit test (extend `filter_tests` or a new small target).
- Visual correctness (blend, page fill, ImGui theme) verified by running the app.

## Out of scope
- Per-layer pen width.
- Paper texture / grain, ink bleed, drop shadows on the page.
- Dimming/hiding non-path layers in paper mode.
- SVG/print export.
