# Fast Marching Contour Filter — Design

Date: 2026-06-25

## Goal

A `Bitmap → PathSet` filter that produces topographic iso-line contours by
running the Fast Marching Method (Eikonal solver). A wavefront expands from a
seed point; local wave speed is controlled by pixel brightness (bright = fast,
rings spread apart; dark = slow, rings compress, adding detail). Slicing the
resulting travel-time map at evenly-spaced levels yields the contour plot.

## Placement

- `src/filters/bitmap/FastMarchingFilter.{h,cpp}`
- Type: `FastMarchingFilter : public FilterTyped<Bitmap, PathSet>`
- Registered in `src/filters/FilterRegistry.cpp` `initDefaults()` as
  `LayerKind::Bitmap → LayerKind::PathSet`, name `"Fast Marching"`.
- Synchronous `applyTyped`. FilterChain already runs filters under `std::async`,
  so the UI does not block.

## Parameters (all `FilterParameter` floats)

| key           | range       | default | type  | meaning |
|---------------|-------------|---------|-------|---------|
| `seedX`       | 0.0 – 1.0   | 0.5     | Float | normalized seed x (× width → pixel) |
| `seedY`       | 0.0 – 1.0   | 0.5     | Float | normalized seed y (× height → pixel) |
| `minSpeed`    | 0.01 – 1.0  | 0.05    | Float | speed floor so dark areas don't stall to ∞ |
| `clearAbove`  | 0.0 – 1.0   | 1.0     | Float | brightness above which pixels cost zero time (blank, no lines); 1.0 = off |
| `contrast`    | 0.1 – 5.0   | 1.0     | Float | gamma applied to brightness before mapping |
| `invert`      | 0/1         | 0       | Bool  | brightness → 1 − brightness |
| `levelSpacing`| 0.005 – 0.5 | 0.05    | Float | contour spacing as fraction of normalized travel time |

## Algorithm

### Stage 1 — Eikonal solve (Fast Marching Method)

Speed field per pixel:
```
b = pixel / 255
if invert: b = 1 - b
if b >= clearAbove: slowness = 0   // infinite speed → flat plateau, no lines
else: speed = minSpeed + (1 - minSpeed) * pow(b, contrast)   // ∈ [minSpeed, 1]
```

Note on "clearing" bright areas: a *finite* max-speed knob does NOT work, because
the travel-time map is normalized to a fixed `[0,1]` range before slicing. A
global speed scale divides out entirely in normalization (only the ratio of
slownesses matters), so making white uniformly faster just spaces its rings
evenly — it never deletes them. To truly blank a region it must accumulate zero
travel time, i.e. infinite speed (`slowness = 0`). `clearAbove` does exactly
that: pixels brighter than the threshold flood into a flat plateau and produce no
contour lines, with lines bunching cleanly at the cleared/uncleared boundary.
`slowness = 0` is safe in the Eikonal update (`f = 0` → arrival time = min
neighbor time), and the per-pixel local slowness means a dark pixel beyond a
cleared region still gets its full cost — no "overstepping."

Travel time `T` init `+∞`; seed pixel `T = 0`. Min-heap of `(T, idx)` with lazy
deletion. Pop smallest, freeze it (KNOWN), relax its 4-neighbors: each FAR/TRIAL
neighbor's `T` is updated by solving the quadratic Eikonal update from its two
best (min) opposing-axis KNOWN neighbors, using local slowness `1/speed` at the
node being updated. Push updated trial nodes onto the heap. O(N log N).

Eikonal quadratic per node: given best horizontal neighbor time `Tx` and best
vertical neighbor time `Ty` (either may be `+∞`), and `f = 1/speed` (in units of
time per pixel):
- If `|Tx - Ty| >= f`: `T = min(Tx, Ty) + f` (one-sided).
- Else: solve `(T-Tx)^2 + (T-Ty)^2 = f^2` → `T = (Tx+Ty + sqrt(2 f^2 - (Tx-Ty)^2)) / 2`.

### Stage 2 — Normalize

Rescale all finite `T` linearly to `[0, 1]` → `Tn`. Makes `levelSpacing`
resolution- and speed-independent. Unreachable pixels (still `+∞`) excluded from
contouring.

### Stage 3 — Marching squares

For each level `L = spacing, 2·spacing, …` while `L < 1.0`: classic 16-case
per-cell contouring over the `Tn` grid. Linear interpolation of crossing point on
each cell edge. Saddle (cases 5/10) resolved by comparing the cell-center average
to `L`. Emit one or two segments per cell. Output coordinates in mm:
`(x + t) * pixel_size_mm`, `(y + t) * pixel_size_mm`.

### Stage 4 — Stitch

Per level, join the cell segments endpoint-to-endpoint into polylines using a
quantized endpoint hash map (quantize to ~1e-4 mm). When a polyline's tail meets
its head, mark `Path.closed = true`. Append polylines to `out.paths`. Finish with
`out.computeAABB()`.

## Edge cases

- Empty / zero-dimension image, or empty pixels → `out.paths.clear()` and return.
- Seed clamped to a valid pixel index.
- Degenerate `Tn` range (max == min) → no contours.

## Testing

`tests/test_fastmarching_filter.cpp` (Google Test), added to the `filter_tests`
target in `CMakeLists.txt`:

1. Constant mid-gray image, center seed → contours are concentric closed loops
   around the seed; path count ≈ `1/levelSpacing`; all paths `closed == true`;
   contour vertices roughly equidistant from seed (circularity check with a
   tolerance).
2. Horizontal-gradient image → contours are roughly parallel / monotone in x
   (open curves spanning top to bottom), validating that brightness warps the
   metric.
3. Empty image → no paths, no crash.

## Scope / non-goals

- Single seed only (normalized point param). No multi-seed / watershed.
- No SVG/GCode export (project is AxiDraw-only).
- No async generator path; synchronous filter is sufficient for typical sizes.
