# Motion Planner Analysis & Testing Plan

## Architecture Overview

The motion planning system lives in `src/plotters/` and has three layers:

1. **MotionPlanner** (`MotionPlanner.h/cpp`) -- Pure math. Converts a polyline (in page-space mm) into time-sliced CoreXY stepper commands (`MoveSlice{aSteps, bSteps, dtMs}`).
2. **PlotSpooler** (`PlotSpooler.h/cpp`) -- Orchestration. Reorders paths, manages a command queue with high/low water marks, feeds slices to the controller from a worker thread.
3. **AxiDrawController** (`AxidrawController.h/cpp`) -- Serial I/O. Sends EBB `SM` commands to the hardware.

All bugs described below are in layer 1 (MotionPlanner).

---

## Critical Bug: Junction Deviation Formula is Inverted

**File:** `MotionPlanner.cpp:90-94`

```cpp
// CURRENT CODE (wrong)
float sinHalf = std::sqrt(std::max(0.0f, 0.5f * (1.0f - dot)));
//                                                  ^^^
// CORRECT (per grbl/Marlin convention)
float sinHalf = std::sqrt(std::max(0.0f, 0.5f * (1.0f + dot)));
```

The `sinHalf` variable is meant to represent `sin(theta/2)` where `theta` is the *continuation angle* (how much the path continues in the same direction). In grbl, `cos_theta` is computed as the **negative** dot product of consecutive unit direction vectors, giving `sin(theta/2) = sqrt(0.5 * (1 - cos_theta)) = sqrt(0.5 * (1 + dot))`.

The current code uses `(1 - dot)` instead of `(1 + dot)`, which **inverts the behavior completely**:

| Turn type | dot | Current sinHalf | Correct sinHalf | Current junction speed |
|-----------|-----|-----------------|-----------------|------------------------|
| Straight (0°) | 1.0 | 0 (skipped) | 1.0 (R=inf, full speed) | full speed (OK by accident) |
| Gentle 18° | 0.95 | 0.158 -> low R -> **slows down** | 0.99 -> high R -> full speed | **Unnecessarily slow** |
| 90° corner | 0.0 | 0.707 -> high R -> **fast** | 0.707 -> moderate R | **Too fast** |
| Hairpin 180° | -1.0 | 1.0 -> R=inf -> **full speed** | 0.0 -> R~0 -> **must stop** | **Full speed through reversal** |

Additionally, the R formula itself uses `(1 + sinHalf) / (1 - sinHalf)` while the standard grbl/Marlin formula is `sinHalf / (1 - sinHalf)`, which inflates R further.

### Why this causes the offset bug

The plotter runs at full speed through hairpin turns and sharp corners. The stepper motors cannot physically execute these instantaneous direction changes at high speed, so they **lose steps**. Lost steps cause a permanent position offset for all subsequent moves -- exactly matching the bug described in `TODO.md`.

### Fix

```cpp
// Line 90: fix the half-angle computation
float sinHalf = std::sqrt(std::max(0.0f, 0.5f * (1.0f + dot)));

// Line 93: fix the R formula to match grbl
float R = jd * sinHalf / (1.0f - sinHalf);
```

After this fix, the collinear case (`dot=1`) gives `sinHalf=1`, which hits the `(1 - sinHalf)` denominator as near-zero. The existing `if (sinHalf > 1e-6f)` guard won't catch this since sinHalf=1 passes. You need to either:
- Change the guard to also skip when `sinHalf > (1.0f - 1e-6f)` (nearly straight -> keep full speed), or
- Clamp R to some large maximum value

---

## Secondary Issues

### 2. startMm vs pointsPageMm[0] Mismatch

**File:** `MotionPlanner.cpp:118-131`

The velocity profile is precomputed using segment lengths derived from `verts` (which come from `pointsPageMm`). But actual movement starts from `startMm`, which may differ from `pointsPageMm[0]` due to step rounding from previous moves.

For the first segment, the precomputed length/direction is `verts[1] - verts[0]`, but the actual commanded distance is `verts[1] - startMm`. If these differ, the velocity profile doesn't match the real geometry.

In practice this is small (sub-step rounding error), but it means the first segment's acceleration profile is computed for a slightly wrong distance.

### 3. Forward Euler Integration Error

**File:** `MotionPlanner.cpp:179-181`

```cpp
vel += velStep;
timeElapsed += timePer;
pos += vel * timePer;  // forward Euler: uses updated vel for the whole interval
```

This systematically overestimates distance during acceleration (using the higher end-of-interval velocity for the whole interval) and underestimates during deceleration. The error accumulates across slices.

### 4. Velocity Never Reaches Target

**File:** `MotionPlanner.cpp:177`

```cpp
float velStep = (speedLimit - vi) / (static_cast<float>(intervalsUp) + 1.0f);
//                                                                    ^^^
```

The `+ 1` in the denominator means after `intervalsUp` increments, velocity is `vi + intervalsUp * (speedLimit - vi) / (intervalsUp + 1)`, which is always less than `speedLimit`. There's a velocity gap between the accel and cruise phases.

---

## Testing Plan

### CMakeLists.txt Addition

The test target only needs `MotionPlanner.cpp` and `Vec2.h` -- no GUI or serial dependencies:

```cmake
add_executable(motion_planner_tests
  tests/test_motion_planner.cpp
  src/plotters/MotionPlanner.cpp
)
target_include_directories(motion_planner_tests PRIVATE src)
target_link_libraries(motion_planner_tests PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME motion_planner COMMAND motion_planner_tests)
```

### Test File

Already created: `tests/test_motion_planner.cpp`

### Test Cases

| # | Test | What it validates | Expected to fail before fix? |
|---|------|-------------------|------------------------------|
| 1 | `EmptyPath` | < 2 points -> empty result | No |
| 2 | `SinglePoint` | 1 point -> empty result | No |
| 3 | `TwoIdenticalPoints` | Duplicate points trimmed -> empty | No |
| 4 | `AllPointsBelowMinSegment` | All segments < minSegmentMm -> empty | No |
| 5 | `StraightLineHorizontal_PositionConservation` | Sum of steps == expected XY displacement | No |
| 6 | `StraightLineVertical_PositionConservation` | Same, vertical | No |
| 7 | `StraightLineDiagonal_PositionConservation` | Same, diagonal | No |
| 8 | `MultiSegment_PositionConservation` | Closed square: net steps == 0 | No |
| 9 | `FinalPosition_MatchesTarget` | `finalPositionMm` within 1 step of target | No |
| 10 | `FinalPosition_ConsistentWithSteps` | `finalPositionMm` matches step sum | No |
| 11 | `StepRateLimit_StraightLine` | No slice exceeds `maxStepRatePerAxis` | No |
| 12 | `StepRateLimit_PenUp` | Same for pen-up speed | No |
| 13 | `StepRateLimit_DiagonalFast` | Same with high speed setting | No |
| 14 | `StartsAndEndsAtRest` | First/last slice rates << cruise rate | No |
| **15** | **`SharpCorner_SlowerThan_GentleCurve`** | **90deg corner takes more time than gentle curve** | **YES -- catches inverted junction formula** |
| **16** | **`Hairpin_SlowerThan_StraightLine`** | **180deg reversal takes more time than straight** | **YES -- catches inverted junction formula** |
| **17** | **`JunctionSpeed_MonotonicWithAngle`** | **Sharper angle -> more time (monotonic)** | **YES -- catches inverted junction formula** |
| 18 | `CoreXY_DirectionPreserved_Horizontal` | Steps decompose back to +X | No |
| 19 | `CoreXY_DirectionPreserved_Vertical` | Steps decompose back to +Y | No |
| 20 | `CoreXY_DirectionPreserved_NegativeDiagonal` | Steps decompose back to -X,-Y | No |
| 21 | `MinSegment_ShortSegmentsFiltered` | Micro-jitter produces same result as direct line | No |
| 22 | `PenUp_FasterThan_PenDown` | Pen-up total time < pen-down total time | No |
| 23 | `TrapezoidalProfile_LongLine` | Long line shows accel < cruise > decel pattern | No |
| 24 | `Symmetry_ForwardVsReverse` | Forward/reverse paths have equal time and opposite steps | No |
| 25 | `AllSliceDurationsPositive` | Every dtMs >= 1 across various paths | No |
| 26 | `StartOffset_StepsMatchDisplacement` | Steps match `end - startMm` (not `end - pts[0]`) | Possibly |
| 27 | `Circle_PositionConservation` | 64-segment circle: net steps == 0 | No |
| 28 | `Circle_StepRateLimit` | Circle doesn't exceed step rate limit | No |

Tests 15-17 are the key regression tests. They will fail with the current inverted junction formula and pass once the fix is applied.
