#include "filters/bitmap/CmykSeparationFilter.h"

#include <algorithm>
#include <cmath>

namespace
{
	// The density model takes a log of ink transmittance, so it can never see a hard
	// zero. Purely a numerical guard - small enough not to distort the ideal profile.
	constexpr float kMinInkReflectance = 0.001f;

	// Floor on the target colour. Ink on paper bottoms out around 2% reflectance, so
	// asking for anything darker just wastes ink on a black that cannot be reached.
	constexpr float kMinTargetReflectance = 0.02f;

	// Solid-patch reflectance of each ink at full coverage, paper-relative and in
	// LINEAR light (not sRGB-encoded). Rows are C, M, Y, K; columns are R, G, B.
	struct InkProfile
	{
		float t[4][3];
	};

	// Mathematically ideal block dyes: each absorbs exactly one third of the spectrum
	// and passes the rest untouched. Combined with the area-coverage model this
	// reproduces the textbook C = 1 - R separation.
	constexpr InkProfile kIdealInks = {{
		{0.001f, 0.999f, 0.999f},
		{0.999f, 0.001f, 0.999f},
		{0.999f, 0.999f, 0.001f},
		{0.001f, 0.001f, 0.001f},
	}};

	// Typical alcohol-marker / process-ink primaries, converted from measured sRGB
	// swatches to linear reflectance. Real dyes have large unwanted absorptions -
	// cyan is distinctly blue, magenta eats a third of the green - which is exactly
	// why an idealized separation plotted with real pens comes out muddy.
	constexpr InkProfile kMarkerInks = {{
		{0.03f, 0.42f, 0.86f},
		{0.84f, 0.03f, 0.26f},
		{0.95f, 0.88f, 0.03f},
		{0.02f, 0.02f, 0.02f},
	}};

	inline float clamp01(float v)
	{
		return std::max(0.0f, std::min(1.0f, v));
	}

	inline float srgbToLinear(float s)
	{
		// Mixing is only linear in linear light. Skipping this step is the usual
		// reason naive CMYK conversions plot far too pale: 50% sRGB gray is 21%
		// linear reflectance and genuinely needs ~79% ink coverage, not 50%.
		return (s <= 0.04045f) ? (s / 12.92f)
							   : std::pow((s + 0.055f) / 1.055f, 2.4f);
	}

	// Column of the mixing matrix contributed by one ink, per mixing model.
	//  - Area coverage: a half-covered pixel is half ink and half paper, so
	//    reflectance falls linearly and each ink subtracts its absorption (1 - t).
	//  - Optical density: overlapping passes multiply transmittance, which is linear
	//    in -log(t). Better model for markers, which bleed and overlap rather than
	//    tiling the page.
	inline void inkColumn(const float t[3], bool density, float outCol[3])
	{
		for (int ch = 0; ch < 3; ++ch)
		{
			const float tv = std::max(t[ch], kMinInkReflectance);
			outCol[ch] = density ? -std::log(tv) : (1.0f - tv);
		}
	}

	// Target vector the inks have to sum to, in the same space as inkColumn().
	inline void targetVector(float linRgb[3], bool density, float outVec[3])
	{
		for (int ch = 0; ch < 3; ++ch)
		{
			const float v = std::max(linRgb[ch], kMinTargetReflectance);
			outVec[ch] = density ? -std::log(v) : (1.0f - v);
		}
	}

	// Analytic inverse of a column-major 3x3. Returns false if it is singular.
	bool invert3x3(const float m[3][3], float inv[3][3])
	{
		const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
		const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
		const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];

		const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
		if (std::fabs(det) < 1e-8f)
			return false;

		const float invDet = 1.0f / det;
		inv[0][0] = c00 * invDet;
		inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invDet;
		inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;
		inv[1][0] = c01 * invDet;
		inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
		inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * invDet;
		inv[2][0] = c02 * invDet;
		inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * invDet;
		inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;
		return true;
	}

	inline void mul3x3(const float m[3][3], const float v[3], float out[3])
	{
		for (int r = 0; r < 3; ++r)
			out[r] = m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2];
	}
}

void CmykSeparationFilter::applyTyped(const ColorImage &in, Bitmap &out) const
{
	const int channel = std::max(0, std::min(3,
		static_cast<int>(std::lround(m_parameters.at("channel").value))));
	const bool density = m_parameters.at("mixing").value > 0.5f;
	const bool markerInks = m_parameters.at("ink_profile").value > 0.5f;
	const float gcr = clamp01(m_parameters.at("gcr").value);
	const float inkLimit = m_parameters.at("ink_limit").value;
	const float gamma = std::max(0.01f, m_parameters.at("gamma").value);

	out.width_px = in.width_px;
	out.height_px = in.height_px;
	out.pixel_size_mm = in.pixel_size_mm;

	const size_t numPixels = in.width_px * in.height_px;
	out.pixels.assign(numPixels, 255);
	if (numPixels == 0 || in.pixels.size() < numPixels * 3)
		return;

	const InkProfile &inks = markerInks ? kMarkerInks : kIdealInks;

	// A maps ink coverages to the color shift they produce; its columns are the C, M
	// and Y inks and its rows are R, G, B. Solving A * coverage = target is the whole
	// separation. Black is handled separately by the GCR pass below because with four
	// inks and three equations the system would otherwise be underdetermined.
	float colC[3], colM[3], colY[3], colK[3];
	inkColumn(inks.t[0], density, colC);
	inkColumn(inks.t[1], density, colM);
	inkColumn(inks.t[2], density, colY);
	inkColumn(inks.t[3], density, colK);

	float A[3][3];
	for (int r = 0; r < 3; ++r)
	{
		A[r][0] = colC[r];
		A[r][1] = colM[r];
		A[r][2] = colY[r];
	}

	float Ainv[3][3];
	if (!invert3x3(A, Ainv))
		return; // Degenerate ink set: leave the plate blank rather than emit noise.

	// How much each color plate has to give back per unit of black ink laid down.
	// Because the solve is linear this is a constant, so gray component replacement
	// is just walking down the line coverage(k) = coverage(0) - k * blackTradeoff.
	float blackTradeoff[3];
	mul3x3(Ainv, colK, blackTradeoff);

	for (size_t i = 0; i < numPixels; ++i)
	{
		const size_t rgbIdx = i * 3;
		float lin[3];
		for (int ch = 0; ch < 3; ++ch)
			lin[ch] = srgbToLinear(static_cast<float>(in.pixels[rgbIdx + ch]) / 255.0f);

		float target[3];
		targetVector(lin, density, target);

		// How much C, M and Y alone would reproduce the color.
		float cmy[3];
		mul3x3(Ainv, target, cmy);

		// Gray component replacement: trade color ink for black ink while the color
		// still comes out the same. The trade has to stop when the first plate hits
		// zero, since there is no pen that removes ink - that bound is the true
		// neutral content of the pixel, and at gcr = 1 a gray prints with black alone.
		float maxBlack = 1.0f;
		for (int ch = 0; ch < 3; ++ch)
		{
			if (blackTradeoff[ch] > 1e-6f)
				maxBlack = std::min(maxBlack, std::max(0.0f, cmy[ch]) / blackTradeoff[ch]);
		}
		const float k = gcr * clamp01(maxBlack);

		// Negative coverage means the color is outside the gamut of these inks; there
		// is no "remove ink" pen, so clamp and accept the nearest reachable color.
		float cov[4] = {
			clamp01(cmy[0] - k * blackTradeoff[0]),
			clamp01(cmy[1] - k * blackTradeoff[1]),
			clamp01(cmy[2] - k * blackTradeoff[2]),
			clamp01(k)
		};

		const float total = cov[0] + cov[1] + cov[2] + cov[3];
		if (total > inkLimit && total > 0.0f)
		{
			const float scale = inkLimit / total;
			for (int ch = 0; ch < 4; ++ch)
				cov[ch] *= scale;
		}

		float c = clamp01(cov[channel]);
		if (gamma != 1.0f)
			c = std::pow(c, gamma);

		// "Dark = ink", matching what the hatch and stipple filters expect.
		out.pixels[i] = static_cast<uint8_t>(clamp01(1.0f - c) * 255.0f + 0.5f);
	}
}
