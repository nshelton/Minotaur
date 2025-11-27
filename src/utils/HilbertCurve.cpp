#include "utils/HilbertCurve.h"

namespace HilbertCurve {

// Rotate/flip a quadrant appropriately
void rot(int n, int *x, int *y, int rx, int ry) {
    if (ry == 0) {
        if (rx == 1) {
            *x = n - 1 - *x;
            *y = n - 1 - *y;
        }
        // Swap x and y
        int t = *x;
        *x = *y;
        *y = t;
    }
}

// Convert (x, y) to Hilbert curve distance
uint32_t xy2d(int n, int x, int y) {
    uint32_t d = 0;
    for (int s = n / 2; s > 0; s /= 2) {
        int rx = (x & s) > 0;
        int ry = (y & s) > 0;
        d += s * s * ((3 * rx) ^ ry);
        rot(s, &x, &y, rx, ry);
    }
    return d;
}

} // namespace HilbertCurve
