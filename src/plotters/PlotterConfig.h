#pragma once

struct PlotterConfig {
    int penUpPos{16000};
    int penDownPos{12000};
    // Speed percentages (relative to device-safe max steps/sec)
    int drawSpeedPercent{100};   // used when pen is down (plotting)
    int travelSpeedPercent{150}; // used when pen is up (travel moves)
    // Future: auto-connect on launch, device VID/PID allowlist, etc.
};
