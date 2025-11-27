#pragma once

#include <cstdint>

namespace HilbertCurve {

// Convert 2D coordinates to Hilbert curve index
// n: grid size (must be power of 2, e.g., 16, 32, 64, 128)
// x, y: normalized coordinates in range [0, n-1]
// Returns: 1D index along the Hilbert curve
uint32_t xy2d(int n, int x, int y);

// Rotate/flip a quadrant appropriately
void rot(int n, int *x, int *y, int rx, int ry);

} // namespace HilbertCurve
