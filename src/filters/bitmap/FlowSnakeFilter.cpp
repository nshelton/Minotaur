#include "filters/bitmap/FlowSnakeFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

constexpr int NUM_SENSORS = 16;
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;

static inline int clampi(int v, int lo, int hi)
{
	return (v < lo) ? lo : (v > hi ? hi : v);
}

// Bilinear sample of the ink buffer at fractional pixel coordinates.
// Returns ink amount in [0, 1] where 1 = full ink (dark), 0 = no ink (white).
static inline float sampleInk(const std::vector<float> &ink,
                               int W, int H, float x, float y)
{
	if (x < 0.0f || x >= static_cast<float>(W - 1) ||
	    y < 0.0f || y >= static_cast<float>(H - 1))
		return 0.0f;

	int x0 = static_cast<int>(x);
	int y0 = static_cast<int>(y);
	int x1 = x0 + 1;
	int y1 = y0 + 1;

	float fx = x - static_cast<float>(x0);
	float fy = y - static_cast<float>(y0);

	float v00 = ink[static_cast<size_t>(y0) * W + x0];
	float v10 = ink[static_cast<size_t>(y0) * W + x1];
	float v01 = ink[static_cast<size_t>(y1) * W + x0];
	float v11 = ink[static_cast<size_t>(y1) * W + x1];

	return v00 * (1.0f - fx) * (1.0f - fy) +
	       v10 * fx * (1.0f - fy) +
	       v01 * (1.0f - fx) * fy +
	       v11 * fx * fy;
}

// Normalize an angle to [-PI, PI]
static inline float normalizeAngle(float a)
{
	while (a > PI) a -= 2.0f * PI;
	while (a < -PI) a += 2.0f * PI;
	return a;
}

} // anonymous namespace

void FlowSnakeFilter::applyTyped(const Bitmap &in, PathSet &out) const
{
	out.paths.clear();
	out.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

	const int W = static_cast<int>(in.width_px);
	const int H = static_cast<int>(in.height_px);
	if (W <= 0 || H <= 0 || in.pixels.empty())
	{
		out.computeAABB();
		return;
	}

	// --- Read parameters ---
	const float stepSize_mm = std::max(0.1f, m_parameters.at("step_size").value);
	const float momentum = std::clamp(m_parameters.at("momentum").value, 0.0f, 0.99f);
	const float maxTurn_deg = std::max(5.0f, m_parameters.at("max_turn").value);
	const float lookAhead_mm = std::max(1.0f, m_parameters.at("look_ahead").value);
	const float fov_deg = std::clamp(m_parameters.at("fov").value, 10.0f, 360.0f);
	const float eatRadius_mm = std::max(0.1f, m_parameters.at("eat_radius").value);
	const float eatStrength = std::clamp(m_parameters.at("eat_strength").value, 0.01f, 1.0f);
	const int maxSteps = static_cast<int>(std::clamp(
		m_parameters.at("max_steps").value, 1000.0f, 500000.0f));

	// --- Convert mm to pixel space ---
	const float pxPerMm = 1.0f / in.pixel_size_mm;
	const float stepSize_px = stepSize_mm * pxPerMm;
	const float lookAhead_px = lookAhead_mm * pxPerMm;
	const float eatRadius_px = eatRadius_mm * pxPerMm;
	const float maxTurn_rad = maxTurn_deg * DEG_TO_RAD;
	const float fov_rad = fov_deg * DEG_TO_RAD;

	// --- Build mutable ink buffer ---
	// ink[i] in [0, 1]: 1 = full ink (pixel=0, dark), 0 = no ink (pixel=255, white)
	std::vector<float> ink(static_cast<size_t>(W) * H);
	for (size_t i = 0; i < ink.size(); ++i)
	{
		ink[i] = (255.0f - static_cast<float>(in.pixels[i])) / 255.0f;
	}

	// --- Find starting position: pixel with most ink ---
	float maxInk = 0.0f;
	int startIdx = 0;
	for (size_t i = 0; i < ink.size(); ++i)
	{
		if (ink[i] > maxInk)
		{
			maxInk = ink[i];
			startIdx = static_cast<int>(i);
		}
	}

	if (maxInk < 1e-4f)
	{
		out.computeAABB();
		return;
	}

	float posX = static_cast<float>(startIdx % W);
	float posY = static_cast<float>(startIdx / W);

	// --- Initial direction: toward center of mass of nearby ink ---
	float dirAngle = 0.0f;
	{
		float cmx = 0.0f, cmy = 0.0f, totalW = 0.0f;
		int scanR = std::max(1, static_cast<int>(lookAhead_px));
		int px0 = clampi(static_cast<int>(posX) - scanR, 0, W - 1);
		int px1 = clampi(static_cast<int>(posX) + scanR, 0, W - 1);
		int py0 = clampi(static_cast<int>(posY) - scanR, 0, H - 1);
		int py1 = clampi(static_cast<int>(posY) + scanR, 0, H - 1);

		for (int py = py0; py <= py1; ++py)
		{
			for (int px = px0; px <= px1; ++px)
			{
				float w = ink[static_cast<size_t>(py) * W + px];
				cmx += static_cast<float>(px) * w;
				cmy += static_cast<float>(py) * w;
				totalW += w;
			}
		}

		if (totalW > 0.0f)
		{
			cmx /= totalW;
			cmy /= totalW;
			float dx = cmx - posX;
			float dy = cmy - posY;
			if (dx * dx + dy * dy > 1e-6f)
			{
				dirAngle = std::atan2(dy, dx);
			}
		}
	}

	// --- Gaussian consumption kernel ---
	const float sigma = eatRadius_px / 2.0f;
	const float sigma2x2 = 2.0f * sigma * sigma;
	const int eatRadiusI = static_cast<int>(std::ceil(eatRadius_px));

	// --- Sensing ray sampling interval ---
	const float sampleStep = std::max(1.0f, stepSize_px * 0.5f);
	const int numRaySamples = std::max(1, static_cast<int>(lookAhead_px / sampleStep));

	// --- Main simulation loop ---
	Path path;
	path.closed = false;
	path.points.reserve(static_cast<size_t>(maxSteps) + 1);
	path.points.emplace_back(posX * in.pixel_size_mm, posY * in.pixel_size_mm);

	for (int step = 0; step < maxSteps; ++step)
	{
		if ((step & 0xFF) == 0)  // report every 256 steps to avoid overhead
			setProgress(static_cast<float>(step) / static_cast<float>(maxSteps));

		// === Sensing: cast rays in a fan across the FOV ===
		float desiredDx = 0.0f;
		float desiredDy = 0.0f;

		for (int s = 0; s < NUM_SENSORS; ++s)
		{
			float angleOffset;
			if (NUM_SENSORS > 1)
				angleOffset = -fov_rad / 2.0f +
				              static_cast<float>(s) * fov_rad /
				                  static_cast<float>(NUM_SENSORS - 1);
			else
				angleOffset = 0.0f;

			float rayAngle = dirAngle + angleOffset;
			float rayDx = std::cos(rayAngle);
			float rayDy = std::sin(rayAngle);

			// Accumulate ink along this ray, weighted by inverse distance
			float score = 0.0f;
			for (int j = 1; j <= numRaySamples; ++j)
			{
				float dist = sampleStep * static_cast<float>(j);
				float sx = posX + rayDx * dist;
				float sy = posY + rayDy * dist;

				float inkVal = sampleInk(ink, W, H, sx, sy);
				score += inkVal / dist;
			}

			desiredDx += rayDx * score;
			desiredDy += rayDy * score;
		}

		// === Compute new direction ===
		// When ink is sensed, steer toward it. When no ink is sensed,
		// desiredDx/desiredDy are zero and momentum carries the snake forward.
		float newAngle;
		float desiredLen = std::sqrt(desiredDx * desiredDx + desiredDy * desiredDy);

		if (desiredLen > 1e-6f)
		{
			desiredDx /= desiredLen;
			desiredDy /= desiredLen;

			float desiredAngle = std::atan2(desiredDy, desiredDx);

			// Angular difference, normalized to [-PI, PI]
			float angleDiff = normalizeAngle(desiredAngle - dirAngle);

			// Apply momentum: higher momentum = less turn toward ink
			float blendedDiff = angleDiff * (1.0f - momentum);

			// Clamp to max turn rate
			blendedDiff = std::clamp(blendedDiff, -maxTurn_rad, maxTurn_rad);

			newAngle = dirAngle + blendedDiff;
		}
		else
		{
			// No ink anywhere: keep going straight (momentum carries)
			newAngle = dirAngle;
		}

		// === Advance position ===
		float dx = std::cos(newAngle) * stepSize_px;
		float dy = std::sin(newAngle) * stepSize_px;
		float newX = posX + dx;
		float newY = posY + dy;

		// Boundary reflection
		if (newX < 0.0f)
		{
			newX = -newX;
			newAngle = PI - newAngle;
		}
		if (newX >= static_cast<float>(W))
		{
			newX = 2.0f * static_cast<float>(W) - newX - 1.0f;
			newAngle = PI - newAngle;
		}
		if (newY < 0.0f)
		{
			newY = -newY;
			newAngle = -newAngle;
		}
		if (newY >= static_cast<float>(H))
		{
			newY = 2.0f * static_cast<float>(H) - newY - 1.0f;
			newAngle = -newAngle;
		}

		// Safety clamp
		newX = std::clamp(newX, 0.0f, static_cast<float>(W - 1));
		newY = std::clamp(newY, 0.0f, static_cast<float>(H - 1));

		posX = newX;
		posY = newY;
		dirAngle = normalizeAngle(newAngle);

		// === Consume ink under the snake (Gaussian kernel) ===
		int cx0 = clampi(static_cast<int>(posX) - eatRadiusI, 0, W - 1);
		int cx1 = clampi(static_cast<int>(posX) + eatRadiusI, 0, W - 1);
		int cy0 = clampi(static_cast<int>(posY) - eatRadiusI, 0, H - 1);
		int cy1 = clampi(static_cast<int>(posY) + eatRadiusI, 0, H - 1);
		const float eatR2 = eatRadius_px * eatRadius_px;

		for (int py = cy0; py <= cy1; ++py)
		{
			for (int px = cx0; px <= cx1; ++px)
			{
				float ddx = static_cast<float>(px) - posX;
				float ddy = static_cast<float>(py) - posY;
				float d2 = ddx * ddx + ddy * ddy;

				if (d2 <= eatR2)
				{
					float gaussWeight = std::exp(-d2 / sigma2x2);
					size_t idx = static_cast<size_t>(py) * W + px;
					ink[idx] *= (1.0f - eatStrength * gaussWeight);
				}
			}
		}

		// === Drop path node ===
		path.points.emplace_back(posX * in.pixel_size_mm, posY * in.pixel_size_mm);
	}

	setProgress(1.0f);

	// --- Finalize ---
	size_t totalVertices = path.points.size();
	if (totalVertices >= 2)
	{
		out.paths.emplace_back(std::move(path));
	}

	const_cast<FlowSnakeFilter *>(this)->setLastVertexCount(totalVertices);
	const_cast<FlowSnakeFilter *>(this)->setLastPathCount(out.paths.size());

	out.computeAABB();
}
