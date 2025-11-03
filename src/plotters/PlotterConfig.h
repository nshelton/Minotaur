#pragma once

struct PlotterConfig {
    int penUpPos{17548};
    int penDownPos{14058};
    // Speed percentages (relative to device-safe max steps/sec)
    int drawSpeedPercent{128};   // used when pen is down (plotting)
    int travelSpeedPercent{154}; // used when pen is up (travel moves)
    // Motion planner (mm-based)
    float drawSpeedMmPerS{38.20000076293945f};
    float travelSpeedMmPerS{121.9000015258789f};
    float accelDrawMmPerS2{1132.0f};
    float accelTravelMmPerS2{3112.0f};
    float cornering{0.3100000023841858f};
    // Minimum allowed junction speed as a fraction of draw speed (0-100)
    int junctionSpeedFloorPercent{50};
    int timeSliceMs{50};
    int maxStepRatePerAxis{5296}; // steps/second per axis
    float minSegmentMm{0.5099999904632568f};
    // Future: auto-connect on launch, device VID/PID allowlist, etc.
};
