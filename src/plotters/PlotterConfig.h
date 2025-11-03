#pragma once

struct PlotterConfig {
    int penUpPos{16000};
    int penDownPos{12000};
    // Speed percentages (relative to device-safe max steps/sec)
    int drawSpeedPercent{100};   // used when pen is down (plotting)
    int travelSpeedPercent{150}; // used when pen is up (travel moves)
    // Motion planner (mm-based)
    float drawSpeedMmPerS{60.0f};
    float travelSpeedMmPerS{120.0f};
    float accelDrawMmPerS2{600.0f};
    float accelTravelMmPerS2{1000.0f};
    float cornering{100.0f};
    // Minimum allowed junction speed as a fraction of draw speed (0-100)
    int junctionSpeedFloorPercent{50};
    int timeSliceMs{10};
    int maxStepRatePerAxis{8000}; // steps/second per axis
    float minSegmentMm{0.05f};
    // Future: auto-connect on launch, device VID/PID allowlist, etc.
};
