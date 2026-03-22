#include "filters/bitmap/RotateFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;

// Bilinear sample from source bitmap. Returns 255 (white) for out-of-bounds.
static inline uint8_t sampleBilinear(const std::vector<uint8_t> &pixels,
                                      int W, int H, float x, float y)
{
	if (x < 0.0f || x >= static_cast<float>(W - 1) ||
	    y < 0.0f || y >= static_cast<float>(H - 1))
		return 255;

	int x0 = static_cast<int>(x);
	int y0 = static_cast<int>(y);
	int x1 = x0 + 1;
	int y1 = y0 + 1;

	float fx = x - static_cast<float>(x0);
	float fy = y - static_cast<float>(y0);

	float v00 = static_cast<float>(pixels[static_cast<size_t>(y0) * W + x0]);
	float v10 = static_cast<float>(pixels[static_cast<size_t>(y0) * W + x1]);
	float v01 = static_cast<float>(pixels[static_cast<size_t>(y1) * W + x0]);
	float v11 = static_cast<float>(pixels[static_cast<size_t>(y1) * W + x1]);

	float val = v00 * (1.0f - fx) * (1.0f - fy) +
	            v10 * fx * (1.0f - fy) +
	            v01 * (1.0f - fx) * fy +
	            v11 * fx * fy;

	return static_cast<uint8_t>(std::clamp(val + 0.5f, 0.0f, 255.0f));
}

} // anonymous namespace

void RotateFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
	const int srcW = static_cast<int>(in.width_px);
	const int srcH = static_cast<int>(in.height_px);
	if (srcW <= 0 || srcH <= 0 || in.pixels.empty())
	{
		out.width_px = 0;
		out.height_px = 0;
		out.pixels.clear();
		out.pixel_size_mm = in.pixel_size_mm;
		return;
	}

	const float angleDeg = m_parameters.at("angle").value;

	// Fast path: no rotation
	if (std::fabs(angleDeg) < 1e-4f)
	{
		out.width_px = in.width_px;
		out.height_px = in.height_px;
		out.pixel_size_mm = in.pixel_size_mm;
		out.pixels = in.pixels;
		return;
	}

	const float angleRad = angleDeg * DEG_TO_RAD;
	const float cosA = std::cos(angleRad);
	const float sinA = std::sin(angleRad);

	// Source center
	const float srcCx = static_cast<float>(srcW) * 0.5f;
	const float srcCy = static_cast<float>(srcH) * 0.5f;

	// Compute rotated bounding box by transforming the four corners
	float cornersX[4], cornersY[4];
	float hx[4] = {0.0f, static_cast<float>(srcW), static_cast<float>(srcW), 0.0f};
	float hy[4] = {0.0f, 0.0f, static_cast<float>(srcH), static_cast<float>(srcH)};

	for (int i = 0; i < 4; ++i)
	{
		float dx = hx[i] - srcCx;
		float dy = hy[i] - srcCy;
		cornersX[i] = cosA * dx - sinA * dy;
		cornersY[i] = sinA * dx + cosA * dy;
	}

	float minX = cornersX[0], maxX = cornersX[0];
	float minY = cornersY[0], maxY = cornersY[0];
	for (int i = 1; i < 4; ++i)
	{
		minX = std::min(minX, cornersX[i]);
		maxX = std::max(maxX, cornersX[i]);
		minY = std::min(minY, cornersY[i]);
		maxY = std::max(maxY, cornersY[i]);
	}

	const int dstW = static_cast<int>(std::ceil(maxX - minX));
	const int dstH = static_cast<int>(std::ceil(maxY - minY));
	if (dstW <= 0 || dstH <= 0)
	{
		out.width_px = 0;
		out.height_px = 0;
		out.pixels.clear();
		out.pixel_size_mm = in.pixel_size_mm;
		return;
	}

	// Destination center
	const float dstCx = static_cast<float>(dstW) * 0.5f;
	const float dstCy = static_cast<float>(dstH) * 0.5f;

	// Inverse rotation (dst -> src): rotate by -angle
	const float icosA = cosA;   // cos(-a) = cos(a)
	const float isinA = -sinA;  // sin(-a) = -sin(a)

	out.width_px = static_cast<size_t>(dstW);
	out.height_px = static_cast<size_t>(dstH);
	out.pixel_size_mm = in.pixel_size_mm;
	out.pixels.resize(static_cast<size_t>(dstW) * dstH, 255);

	for (int dy = 0; dy < dstH; ++dy)
	{
		for (int dx = 0; dx < dstW; ++dx)
		{
			// Map destination pixel to source coordinates via inverse rotation
			float rx = static_cast<float>(dx) - dstCx;
			float ry = static_cast<float>(dy) - dstCy;

			float sx = icosA * rx - isinA * ry + srcCx;
			float sy = isinA * rx + icosA * ry + srcCy;

			out.pixels[static_cast<size_t>(dy) * dstW + dx] =
				sampleBilinear(in.pixels, srcW, srcH, sx, sy);
		}
	}
}
