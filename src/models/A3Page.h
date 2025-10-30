#pragma once

struct A3Page {
    // Dimensions in millimeters (ISO 216): A3 = 297 x 420
    float width_mm{297.0f};
    float height_mm{420.0f};

    // Orientation control; if landscape, swap width/height at construction time
    static A3Page Portrait() { return A3Page{297.0f, 420.0f}; }
    static A3Page Landscape() { return A3Page{420.0f, 297.0f}; }
};

