#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <vector>

#include "core/Vec2.h"
#include "plotters/MotionPlanner.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static PlannerSettings defaultSettings() {
    PlannerSettings s;
    s.speedPenDownMmPerS   = 60.0f;
    s.speedPenUpMmPerS     = 120.0f;
    s.accelPenDownMmPerS2  = 600.0f;
    s.accelPenUpMmPerS2    = 1000.0f;
    s.cornering            = 0.31f;
    s.junctionSpeedFloorPercent = 50;
    s.timeSliceMs          = 10;
    s.maxStepRatePerAxis   = 8000;
    s.minSegmentMm         = 0.05f;
    s.stepsPerMm           = 80;
    return s;
}

// Sum all aSteps / bSteps across the move list
static void sumSteps(const std::vector<MoveSlice> &moves, int &totalA, int &totalB) {
    totalA = 0;
    totalB = 0;
    for (const auto &m : moves) {
        totalA += m.aSteps;
        totalB += m.bSteps;
    }
}

// Total duration in ms
static int totalDurationMs(const std::vector<MoveSlice> &moves) {
    int t = 0;
    for (const auto &m : moves) t += m.dtMs;
    return t;
}

// Convert cumulative CoreXY steps back to XY mm
// The planner outputs axis1 = A = dx+dy (in steps), axis2 = -B = -(dx-dy) (in steps)
// So: dxSteps = 0.5*(axis1 - axis2), dySteps = 0.5*(axis1 + axis2)
static Vec2 stepsToMm(int totalA, int totalB, int stepsPerMm) {
    float dxSteps = 0.5f * (static_cast<float>(totalA) - static_cast<float>(totalB));
    float dySteps = 0.5f * (static_cast<float>(totalA) + static_cast<float>(totalB));
    return Vec2(dxSteps / static_cast<float>(stepsPerMm),
                dySteps / static_cast<float>(stepsPerMm));
}

// Max step rate (steps/sec) observed in any single slice, for either axis
static float maxObservedStepRate(const std::vector<MoveSlice> &moves) {
    float worst = 0.0f;
    for (const auto &m : moves) {
        float dtSec = static_cast<float>(m.dtMs) / 1000.0f;
        if (dtSec <= 0.0f) continue;
        float rateA = std::abs(static_cast<float>(m.aSteps)) / dtSec;
        float rateB = std::abs(static_cast<float>(m.bSteps)) / dtSec;
        worst = std::max(worst, std::max(rateA, rateB));
    }
    return worst;
}

// ===========================================================================
// 1. Degenerate Input Handling
// ===========================================================================

TEST(MotionPlanner, EmptyPath) {
    auto s = defaultSettings();
    Vec2 start(10.0f, 20.0f);
    std::vector<Vec2> pts; // empty
    auto result = planPath(s, pts, false, start);
    EXPECT_TRUE(result.moves.empty());
    EXPECT_FLOAT_EQ(result.finalPositionMm.x, start.x);
    EXPECT_FLOAT_EQ(result.finalPositionMm.y, start.y);
}

TEST(MotionPlanner, SinglePoint) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { Vec2(5.0f, 5.0f) };
    auto result = planPath(s, pts, false, start);
    EXPECT_TRUE(result.moves.empty());
}

TEST(MotionPlanner, TwoIdenticalPoints) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    Vec2 p(3.0f, 4.0f);
    std::vector<Vec2> pts = { p, p };
    auto result = planPath(s, pts, false, start);
    // After trimming, only 1 vertex remains → no segments → empty
    EXPECT_TRUE(result.moves.empty());
}

TEST(MotionPlanner, AllPointsBelowMinSegment) {
    auto s = defaultSettings();
    s.minSegmentMm = 1.0f;
    Vec2 start(0.0f, 0.0f);
    // All points within 0.01mm of each other
    std::vector<Vec2> pts = {
        Vec2(0.0f, 0.0f),
        Vec2(0.005f, 0.005f),
        Vec2(0.008f, 0.002f),
    };
    auto result = planPath(s, pts, false, start);
    EXPECT_TRUE(result.moves.empty());
}

// ===========================================================================
// 2. Position Conservation — total steps match expected displacement
// ===========================================================================

TEST(MotionPlanner, StraightLineHorizontal_PositionConservation) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    Vec2 end(20.0f, 0.0f);
    std::vector<Vec2> pts = { start, end };
    auto result = planPath(s, pts, false, start);

    ASSERT_FALSE(result.moves.empty());

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 actualMm = stepsToMm(totalA, totalB, s.stepsPerMm);

    // Should be within 1 step of target (rounding tolerance = 1/stepsPerMm)
    float tol = 1.0f / static_cast<float>(s.stepsPerMm) + 0.001f;
    EXPECT_NEAR(actualMm.x, end.x - start.x, tol) << "X displacement mismatch";
    EXPECT_NEAR(actualMm.y, end.y - start.y, tol) << "Y displacement mismatch";
}

TEST(MotionPlanner, StraightLineVertical_PositionConservation) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    Vec2 end(0.0f, 15.0f);
    std::vector<Vec2> pts = { start, end };
    auto result = planPath(s, pts, false, start);

    ASSERT_FALSE(result.moves.empty());

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 actualMm = stepsToMm(totalA, totalB, s.stepsPerMm);

    float tol = 1.0f / static_cast<float>(s.stepsPerMm) + 0.001f;
    EXPECT_NEAR(actualMm.x, 0.0f, tol);
    EXPECT_NEAR(actualMm.y, 15.0f, tol);
}

TEST(MotionPlanner, StraightLineDiagonal_PositionConservation) {
    auto s = defaultSettings();
    Vec2 start(5.0f, 5.0f);
    Vec2 end(25.0f, 35.0f);
    std::vector<Vec2> pts = { start, end };
    auto result = planPath(s, pts, false, start);

    ASSERT_FALSE(result.moves.empty());

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 actualMm = stepsToMm(totalA, totalB, s.stepsPerMm);

    float tol = 1.0f / static_cast<float>(s.stepsPerMm) + 0.001f;
    EXPECT_NEAR(actualMm.x, 20.0f, tol);
    EXPECT_NEAR(actualMm.y, 30.0f, tol);
}

TEST(MotionPlanner, MultiSegment_PositionConservation) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = {
        Vec2(0.0f, 0.0f),
        Vec2(10.0f, 0.0f),
        Vec2(10.0f, 10.0f),
        Vec2(0.0f, 10.0f),
        Vec2(0.0f, 0.0f),  // closed square
    };
    auto result = planPath(s, pts, false, start);
    ASSERT_FALSE(result.moves.empty());

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 actualMm = stepsToMm(totalA, totalB, s.stepsPerMm);

    // Closed path: net displacement should be ~0
    float tol = 2.0f / static_cast<float>(s.stepsPerMm) + 0.01f;
    EXPECT_NEAR(actualMm.x, 0.0f, tol) << "Closed path X should return to origin";
    EXPECT_NEAR(actualMm.y, 0.0f, tol) << "Closed path Y should return to origin";
}

// ===========================================================================
// 3. Final Position Accuracy
// ===========================================================================

TEST(MotionPlanner, FinalPosition_MatchesTarget) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    Vec2 end(17.3f, 8.9f);
    std::vector<Vec2> pts = { start, end };
    auto result = planPath(s, pts, false, start);

    // finalPositionMm should be within step rounding of end
    float tol = 1.0f / static_cast<float>(s.stepsPerMm) + 0.02f;
    EXPECT_NEAR(result.finalPositionMm.x, end.x, tol);
    EXPECT_NEAR(result.finalPositionMm.y, end.y, tol);
}

TEST(MotionPlanner, FinalPosition_ConsistentWithSteps) {
    auto s = defaultSettings();
    Vec2 start(3.0f, 7.0f);
    Vec2 end(23.0f, 17.0f);
    std::vector<Vec2> pts = { start, end };
    auto result = planPath(s, pts, false, start);

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 stepDelta = stepsToMm(totalA, totalB, s.stepsPerMm);
    Vec2 posFromSteps(start.x + stepDelta.x, start.y + stepDelta.y);

    // finalPositionMm should match what the steps actually achieve
    float tol = 1.0f / static_cast<float>(s.stepsPerMm) + 0.02f;
    EXPECT_NEAR(result.finalPositionMm.x, posFromSteps.x, tol);
    EXPECT_NEAR(result.finalPositionMm.y, posFromSteps.y, tol);
}

// ===========================================================================
// 4. Step Rate Limits — no slice exceeds maxStepRatePerAxis
// ===========================================================================

TEST(MotionPlanner, StepRateLimit_StraightLine) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(50.0f, 0.0f) };
    auto result = planPath(s, pts, false, start);

    float maxRate = maxObservedStepRate(result.moves);
    // Allow 1% tolerance for rounding
    EXPECT_LE(maxRate, static_cast<float>(s.maxStepRatePerAxis) * 1.01f)
        << "Step rate " << maxRate << " exceeds limit " << s.maxStepRatePerAxis;
}

TEST(MotionPlanner, StepRateLimit_PenUp) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(50.0f, 50.0f) };
    auto result = planPath(s, pts, true, start);

    float maxRate = maxObservedStepRate(result.moves);
    EXPECT_LE(maxRate, static_cast<float>(s.maxStepRatePerAxis) * 1.01f)
        << "Pen-up step rate " << maxRate << " exceeds limit " << s.maxStepRatePerAxis;
}

TEST(MotionPlanner, StepRateLimit_DiagonalFast) {
    auto s = defaultSettings();
    s.speedPenUpMmPerS = 200.0f;  // push speed higher
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(100.0f, 100.0f) };
    auto result = planPath(s, pts, true, start);

    float maxRate = maxObservedStepRate(result.moves);
    EXPECT_LE(maxRate, static_cast<float>(s.maxStepRatePerAxis) * 1.01f);
}

// ===========================================================================
// 5. Velocity Boundary Conditions — starts/ends at rest
// ===========================================================================

TEST(MotionPlanner, StartsAndEndsAtRest) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(30.0f, 0.0f) };
    auto result = planPath(s, pts, false, start);
    ASSERT_GE(result.moves.size(), 3u) << "Need enough slices to check ramp";

    // First slice: step rate should be low (accelerating from 0)
    const auto &first = result.moves.front();
    float dtFirst = static_cast<float>(first.dtMs) / 1000.0f;
    float rateFirst = std::hypot(static_cast<float>(first.aSteps),
                                  static_cast<float>(first.bSteps)) / std::max(1e-6f, dtFirst);
    // At rest, the first slice rate should be well below cruise rate
    float cruiseStepRate = s.speedPenDownMmPerS * s.stepsPerMm * std::sqrt(2.0f); // worst case diagonal
    EXPECT_LT(rateFirst, cruiseStepRate * 0.6f)
        << "First slice step rate too high — not starting from rest";

    // Last slice: step rate should be low (decelerating to 0)
    const auto &last = result.moves.back();
    float dtLast = static_cast<float>(last.dtMs) / 1000.0f;
    float rateLast = std::hypot(static_cast<float>(last.aSteps),
                                 static_cast<float>(last.bSteps)) / std::max(1e-6f, dtLast);
    EXPECT_LT(rateLast, cruiseStepRate * 0.6f)
        << "Last slice step rate too high — not ending at rest";
}

// ===========================================================================
// 6. Junction Speed vs Corner Angle (catches inverted formula)
// ===========================================================================

// Sharp corners should be slower (take more time) than gentle curves
// for equal total distance
TEST(MotionPlanner, SharpCorner_SlowerThan_GentleCurve) {
    auto s = defaultSettings();
    s.junctionSpeedFloorPercent = 0; // disable floor to isolate junction logic

    // Path 1: gentle 170° bend (nearly straight) — total ~20mm
    Vec2 start1(0.0f, 0.0f);
    std::vector<Vec2> gentle = {
        Vec2(0.0f, 0.0f),
        Vec2(10.0f, 0.0f),
        Vec2(20.0f, 0.87f),  // ~5° deviation
    };
    auto gentleResult = planPath(s, gentle, false, start1);

    // Path 2: sharp 90° corner — total ~20mm
    Vec2 start2(0.0f, 0.0f);
    std::vector<Vec2> sharp = {
        Vec2(0.0f, 0.0f),
        Vec2(10.0f, 0.0f),
        Vec2(10.0f, 10.0f),  // 90° turn
    };
    auto sharpResult = planPath(s, sharp, false, start2);

    int gentleTime = totalDurationMs(gentleResult.moves);
    int sharpTime  = totalDurationMs(sharpResult.moves);

    EXPECT_GT(sharpTime, gentleTime)
        << "90° corner (" << sharpTime << "ms) should take MORE time than gentle curve ("
        << gentleTime << "ms) — junction speed should be lower at sharp corners";
}

TEST(MotionPlanner, Hairpin_SlowerThan_StraightLine) {
    auto s = defaultSettings();
    s.junctionSpeedFloorPercent = 0;

    // Path 1: straight line 20mm
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> straight = {
        Vec2(0.0f, 0.0f),
        Vec2(10.0f, 0.0f),
        Vec2(20.0f, 0.0f),
    };
    auto straightResult = planPath(s, straight, false, start);

    // Path 2: hairpin — go 10mm right, reverse, go 10mm right again
    std::vector<Vec2> hairpin = {
        Vec2(0.0f, 0.0f),
        Vec2(10.0f, 0.0f),
        Vec2(0.0f, 0.0f),   // 180° reversal
    };
    auto hairpinResult = planPath(s, hairpin, false, start);

    int straightTime = totalDurationMs(straightResult.moves);
    int hairpinTime  = totalDurationMs(hairpinResult.moves);

    // Hairpin must decelerate to ~0 at the corner, so it should take significantly longer
    EXPECT_GT(hairpinTime, straightTime)
        << "Hairpin (" << hairpinTime << "ms) must be slower than straight line ("
        << straightTime << "ms) — must decelerate at 180° reversal";
}

TEST(MotionPlanner, JunctionSpeed_MonotonicWithAngle) {
    // As corner angle gets sharper, total time should increase (or stay same)
    auto s = defaultSettings();
    s.junctionSpeedFloorPercent = 0;

    Vec2 start(0.0f, 0.0f);
    float prevTime = 0.0f;
    // Test angles: 5°, 30°, 60°, 90°, 135°, 170° deviations
    float angles[] = { 5.0f, 30.0f, 60.0f, 90.0f, 135.0f, 170.0f };

    for (float angleDeg : angles) {
        float angleRad = angleDeg * 3.14159265f / 180.0f;
        // Second segment deviates by angleDeg from horizontal
        Vec2 p2(10.0f + 10.0f * std::cos(angleRad),
                10.0f * std::sin(angleRad));
        std::vector<Vec2> pts = {
            Vec2(0.0f, 0.0f),
            Vec2(10.0f, 0.0f),
            p2,
        };
        auto result = planPath(s, pts, false, start);
        float t = static_cast<float>(totalDurationMs(result.moves));

        if (prevTime > 0.0f) {
            EXPECT_GE(t, prevTime * 0.95f)  // 5% tolerance for numerical noise
                << "Time should increase with sharper corners. angle=" << angleDeg
                << "° time=" << t << "ms, prev=" << prevTime << "ms";
        }
        prevTime = t;
    }
}

// ===========================================================================
// 7. CoreXY Roundtrip — steps decompose back to correct direction
// ===========================================================================

TEST(MotionPlanner, CoreXY_DirectionPreserved_Horizontal) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(10.0f, 0.0f) };
    auto result = planPath(s, pts, false, start);

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 mm = stepsToMm(totalA, totalB, s.stepsPerMm);

    EXPECT_GT(mm.x, 0.0f) << "Should move in +X direction";
    EXPECT_NEAR(mm.y, 0.0f, 0.02f) << "Should have no Y movement";
}

TEST(MotionPlanner, CoreXY_DirectionPreserved_Vertical) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(0.0f, 10.0f) };
    auto result = planPath(s, pts, false, start);

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 mm = stepsToMm(totalA, totalB, s.stepsPerMm);

    EXPECT_NEAR(mm.x, 0.0f, 0.02f) << "Should have no X movement";
    EXPECT_GT(mm.y, 0.0f) << "Should move in +Y direction";
}

TEST(MotionPlanner, CoreXY_DirectionPreserved_NegativeDiagonal) {
    auto s = defaultSettings();
    Vec2 start(20.0f, 20.0f);
    Vec2 end(5.0f, 10.0f);
    std::vector<Vec2> pts = { start, end };
    auto result = planPath(s, pts, false, start);

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 mm = stepsToMm(totalA, totalB, s.stepsPerMm);

    EXPECT_LT(mm.x, 0.0f) << "Should move in -X direction";
    EXPECT_LT(mm.y, 0.0f) << "Should move in -Y direction";
}

// ===========================================================================
// 8. Minimum Segment Filtering
// ===========================================================================

TEST(MotionPlanner, MinSegment_ShortSegmentsFiltered) {
    auto s = defaultSettings();
    s.minSegmentMm = 1.0f;
    Vec2 start(0.0f, 0.0f);

    // Path with micro-jitter between two real waypoints
    std::vector<Vec2> pts = {
        Vec2(0.0f, 0.0f),
        Vec2(0.001f, 0.001f),  // too short, should be filtered
        Vec2(0.002f, 0.001f),  // too short, should be filtered
        Vec2(10.0f, 0.0f),    // real destination
    };
    auto result = planPath(s, pts, false, start);
    ASSERT_FALSE(result.moves.empty());

    // The result should be basically identical to a direct line from (0,0) to (10,0)
    std::vector<Vec2> direct = { Vec2(0.0f, 0.0f), Vec2(10.0f, 0.0f) };
    auto directResult = planPath(s, direct, false, start);

    int totalA1, totalB1, totalA2, totalB2;
    sumSteps(result.moves, totalA1, totalB1);
    sumSteps(directResult.moves, totalA2, totalB2);

    EXPECT_EQ(totalA1, totalA2) << "Filtered path should produce same steps as direct";
    EXPECT_EQ(totalB1, totalB2);
}

// ===========================================================================
// 9. Pen Up vs Pen Down — pen up should be faster
// ===========================================================================

TEST(MotionPlanner, PenUp_FasterThan_PenDown) {
    auto s = defaultSettings();
    // Ensure pen-up speed is actually higher
    ASSERT_GT(s.speedPenUpMmPerS, s.speedPenDownMmPerS);

    Vec2 start(0.0f, 0.0f);
    std::vector<Vec2> pts = { start, Vec2(40.0f, 0.0f) };

    auto penDownResult = planPath(s, pts, false, start);
    auto penUpResult   = planPath(s, pts, true,  start);

    int penDownTime = totalDurationMs(penDownResult.moves);
    int penUpTime   = totalDurationMs(penUpResult.moves);

    EXPECT_LT(penUpTime, penDownTime)
        << "Pen-up (" << penUpTime << "ms) should be faster than pen-down ("
        << penDownTime << "ms)";
}

// ===========================================================================
// 10. Trapezoidal Profile Shape — long line has accel, cruise, decel
// ===========================================================================

TEST(MotionPlanner, TrapezoidalProfile_LongLine) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);
    // Long enough to reach cruise speed
    std::vector<Vec2> pts = { start, Vec2(100.0f, 0.0f) };
    auto result = planPath(s, pts, false, start);

    ASSERT_GE(result.moves.size(), 5u) << "Long line should produce several slices";

    // Compute step rates per slice
    std::vector<float> rates;
    for (const auto &m : result.moves) {
        float dt = static_cast<float>(m.dtMs) / 1000.0f;
        if (dt <= 0.0f) continue;
        float rate = std::hypot(static_cast<float>(m.aSteps),
                                 static_cast<float>(m.bSteps)) / dt;
        rates.push_back(rate);
    }

    ASSERT_GE(rates.size(), 3u);

    // First rate should be lower than middle (acceleration)
    float firstRate = rates.front();
    float midRate   = rates[rates.size() / 2];
    float lastRate  = rates.back();

    EXPECT_LT(firstRate, midRate * 0.9f)
        << "First slice should be slower than cruise (accel phase)";
    EXPECT_LT(lastRate, midRate * 0.9f)
        << "Last slice should be slower than cruise (decel phase)";
}

// ===========================================================================
// 11. Symmetry — forward vs reverse path
// ===========================================================================

TEST(MotionPlanner, Symmetry_ForwardVsReverse) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);

    std::vector<Vec2> fwd = {
        Vec2(0.0f, 0.0f),
        Vec2(15.0f, 0.0f),
        Vec2(15.0f, 10.0f),
    };

    // Reversed path, starting from where fwd ends
    Vec2 revStart(15.0f, 10.0f);
    std::vector<Vec2> rev = {
        Vec2(15.0f, 10.0f),
        Vec2(15.0f, 0.0f),
        Vec2(0.0f, 0.0f),
    };

    auto fwdResult = planPath(s, fwd, false, start);
    auto revResult = planPath(s, rev, false, revStart);

    int fwdTime = totalDurationMs(fwdResult.moves);
    int revTime = totalDurationMs(revResult.moves);

    // Should be approximately equal (within 15% tolerance for rounding)
    float ratio = static_cast<float>(fwdTime) / static_cast<float>(revTime);
    EXPECT_GT(ratio, 0.85f) << "Forward/reverse time ratio too skewed";
    EXPECT_LT(ratio, 1.15f) << "Forward/reverse time ratio too skewed";

    // Net displacement should be equal and opposite
    int fA, fB, rA, rB;
    sumSteps(fwdResult.moves, fA, fB);
    sumSteps(revResult.moves, rA, rB);
    float tol = 2.0f; // steps
    EXPECT_NEAR(static_cast<float>(fA), static_cast<float>(-rA), tol);
    EXPECT_NEAR(static_cast<float>(fB), static_cast<float>(-rB), tol);
}

// ===========================================================================
// 12. All slice durations must be positive
// ===========================================================================

TEST(MotionPlanner, AllSliceDurationsPositive) {
    auto s = defaultSettings();
    Vec2 start(0.0f, 0.0f);

    // Try a variety of paths
    std::vector<std::vector<Vec2>> paths = {
        { Vec2(0,0), Vec2(10,0) },
        { Vec2(0,0), Vec2(0,10) },
        { Vec2(0,0), Vec2(5,5), Vec2(10,0) },
        { Vec2(0,0), Vec2(1,0), Vec2(1,1), Vec2(0,1), Vec2(0,0) },
        { Vec2(0,0), Vec2(50,30) },
    };

    for (size_t p = 0; p < paths.size(); ++p) {
        auto result = planPath(s, paths[p], false, start);
        for (size_t i = 0; i < result.moves.size(); ++i) {
            EXPECT_GE(result.moves[i].dtMs, 1)
                << "Slice " << i << " of path " << p << " has non-positive duration";
        }
    }
}

// ===========================================================================
// 13. startMm offset — planner should handle offset start position
// ===========================================================================

TEST(MotionPlanner, StartOffset_StepsMatchDisplacement) {
    auto s = defaultSettings();
    // Start position differs from path start by step rounding amount
    Vec2 start(0.006f, 0.003f);  // small offset from origin
    Vec2 end(10.0f, 0.0f);
    std::vector<Vec2> pts = { Vec2(0.0f, 0.0f), end };
    auto result = planPath(s, pts, false, start);

    ASSERT_FALSE(result.moves.empty());

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 actualMm = stepsToMm(totalA, totalB, s.stepsPerMm);

    // The actual displacement should get us from start to ~end, not from pts[0] to end
    Vec2 expectedDisplacement(end.x - start.x, end.y - start.y);
    float tol = 2.0f / static_cast<float>(s.stepsPerMm) + 0.02f;
    EXPECT_NEAR(actualMm.x, expectedDisplacement.x, tol);
    EXPECT_NEAR(actualMm.y, expectedDisplacement.y, tol);
}

// ===========================================================================
// 14. Stress: many short segments (polyline approximation of circle)
// ===========================================================================

TEST(MotionPlanner, Circle_PositionConservation) {
    auto s = defaultSettings();
    Vec2 center(20.0f, 20.0f);
    float radius = 10.0f;
    int numPoints = 64;

    std::vector<Vec2> pts;
    for (int i = 0; i <= numPoints; ++i) {
        float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(numPoints);
        pts.push_back(Vec2(center.x + radius * std::cos(angle),
                           center.y + radius * std::sin(angle)));
    }

    Vec2 start = pts.front();
    auto result = planPath(s, pts, false, start);
    ASSERT_FALSE(result.moves.empty());

    int totalA, totalB;
    sumSteps(result.moves, totalA, totalB);
    Vec2 actualMm = stepsToMm(totalA, totalB, s.stepsPerMm);

    // Closed circle: net displacement should be ~0
    float tol = 4.0f / static_cast<float>(s.stepsPerMm) + 0.05f;
    EXPECT_NEAR(actualMm.x, 0.0f, tol) << "Circle X displacement should be ~0";
    EXPECT_NEAR(actualMm.y, 0.0f, tol) << "Circle Y displacement should be ~0";
}

TEST(MotionPlanner, Circle_StepRateLimit) {
    auto s = defaultSettings();
    Vec2 center(20.0f, 20.0f);
    float radius = 10.0f;
    int numPoints = 64;

    std::vector<Vec2> pts;
    for (int i = 0; i <= numPoints; ++i) {
        float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(numPoints);
        pts.push_back(Vec2(center.x + radius * std::cos(angle),
                           center.y + radius * std::sin(angle)));
    }

    Vec2 start = pts.front();
    auto result = planPath(s, pts, false, start);

    float maxRate = maxObservedStepRate(result.moves);
    EXPECT_LE(maxRate, static_cast<float>(s.maxStepRatePerAxis) * 1.01f)
        << "Circle should not exceed step rate limit";
}
