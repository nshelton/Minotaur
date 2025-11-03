#pragma once

#include <vector>
#include <cstdint>

#include "core/Vec2.h"

struct PlannerSettings {
    // Kinematic limits (mm units)
    float speedPenDownMmPerS{60.0f};
    float speedPenUpMmPerS{120.0f};
    float accelPenDownMmPerS2{600.0f};
    float accelPenUpMmPerS2{1000.0f};
    float cornering{100.0f};
    int junctionSpeedFloorPercent{50}; // % of pen-down speed
    int timeSliceMs{10};
    int maxStepRatePerAxis{8000}; // steps/second
    float minSegmentMm{0.05f};
    int stepsPerMm{80};
};

struct MoveSlice {
    int aSteps{0};
    int bSteps{0};
    int dtMs{1};
};

// Plans time-sliced CoreXY motor step deltas (A,B) to traverse the given points.
// pointsPageMm must have at least 2 vertices.
std::vector<MoveSlice> planPath(const PlannerSettings &s,
                                const std::vector<Vec2> &pointsPageMm,
                                bool penUp,
                                const Vec2 &startMm);


