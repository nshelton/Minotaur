#include <gtest/gtest.h>
#include <cmath>
#include "filters/bitmap/CmykSeparationFilter.h"

namespace
{
	enum Plate { C = 0, M = 1, Y = 2, K = 3 };

	// Ink reflectances mirrored from the filter's "Marker CMY" profile so the tests
	// can recombine the separations physically and check the round trip.
	const float kMarkerT[4][3] = {
		{0.03f, 0.42f, 0.86f},
		{0.84f, 0.03f, 0.26f},
		{0.95f, 0.88f, 0.03f},
		{0.02f, 0.02f, 0.02f},
	};

	ColorImage makeSolidColor(int w, int h, uint8_t r, uint8_t g, uint8_t b)
	{
		ColorImage img;
		img.width_px = static_cast<size_t>(w);
		img.height_px = static_cast<size_t>(h);
		img.pixel_size_mm = 1.0f;
		img.pixels.resize(static_cast<size_t>(w) * h * 3);
		for (size_t i = 0; i < img.pixels.size(); i += 3)
		{
			img.pixels[i + 0] = r;
			img.pixels[i + 1] = g;
			img.pixels[i + 2] = b;
		}
		return img;
	}

	float srgbToLinear(float s)
	{
		return (s <= 0.04045f) ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
	}

	uint8_t linearToSrgb8(float lin)
	{
		lin = std::max(0.0f, std::min(1.0f, lin));
		const float s = (lin <= 0.0031308f) ? (lin * 12.92f)
											: (1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f);
		return static_cast<uint8_t>(s * 255.0f + 0.5f);
	}

	// Coverage of one plate, recovered from the "dark = ink" output convention.
	float coverageOf(const CmykSeparationFilter &filter, const ColorImage &in, Plate plate)
	{
		CmykSeparationFilter local;
		local.m_parameters = filter.m_parameters;
		local.setParameter("channel", static_cast<float>(plate));

		Bitmap out;
		local.applyTyped(in, out);
		EXPECT_FALSE(out.pixels.empty());
		return 1.0f - (static_cast<float>(out.pixels[0]) / 255.0f);
	}

	// Recombine all four plates through the same physical model the filter solves,
	// yielding the linear RGB the paper should end up reflecting.
	void reconstruct(const float cov[4], bool density, float outLinRgb[3])
	{
		for (int ch = 0; ch < 3; ++ch)
		{
			if (density)
			{
				// Overlapping passes multiply transmittance.
				float v = 1.0f;
				for (int ink = 0; ink < 4; ++ink)
					v *= std::pow(kMarkerT[ink][ch], cov[ink]);
				outLinRgb[ch] = v;
			}
			else
			{
				// Each ink subtracts absorption proportional to the area it covers.
				float v = 1.0f;
				for (int ink = 0; ink < 4; ++ink)
					v -= cov[ink] * (1.0f - kMarkerT[ink][ch]);
				outLinRgb[ch] = v;
			}
		}
	}

	void allPlates(const CmykSeparationFilter &filter, const ColorImage &in, float outCov[4])
	{
		outCov[C] = coverageOf(filter, in, C);
		outCov[M] = coverageOf(filter, in, M);
		outCov[Y] = coverageOf(filter, in, Y);
		outCov[K] = coverageOf(filter, in, K);
	}

	// Build a swatch by actually mixing known ink coverages, so the target colour is
	// reachable by construction. Hand-picked RGB values are usually outside the gamut
	// of real markers and would only ever test the clamping path.
	ColorImage swatchFromCoverage(const float cov[4], bool density)
	{
		float lin[3];
		reconstruct(cov, density, lin);
		return makeSolidColor(2, 2,
							  linearToSrgb8(lin[0]),
							  linearToSrgb8(lin[1]),
							  linearToSrgb8(lin[2]));
	}
}

// The reason this filter exists: a colour that is near none of the primaries still
// has to be built by overlapping two of them. Color Picker cannot express this.
TEST(CmykSeparationFilter, GreenIsBuiltFromCyanAndYellow)
{
	ColorImage green = makeSolidColor(4, 4, 0, 200, 60);

	CmykSeparationFilter filter;
	filter.setParameter("gcr", 0.0f);

	float cov[4];
	allPlates(filter, green, cov);

	EXPECT_GT(cov[C], 0.5f) << "cyan plate should carry most of the green";
	EXPECT_GT(cov[Y], 0.5f) << "yellow plate should carry most of the green";
	EXPECT_LT(cov[M], 0.2f) << "magenta is the complement of green, expect little";
}

TEST(CmykSeparationFilter, WhiteLeavesEveryPlateBlank)
{
	ColorImage white = makeSolidColor(4, 4, 255, 255, 255);

	CmykSeparationFilter filter;
	float cov[4];
	allPlates(filter, white, cov);

	for (int ink = 0; ink < 4; ++ink)
		EXPECT_NEAR(cov[ink], 0.0f, 0.01f) << "plate " << ink;
}

TEST(CmykSeparationFilter, BlackGoesToTheBlackPlateUnderFullGcr)
{
	ColorImage black = makeSolidColor(4, 4, 0, 0, 0);

	CmykSeparationFilter filter;
	filter.setParameter("gcr", 1.0f);
	filter.setParameter("ink_limit", 4.0f);

	float cov[4];
	allPlates(filter, black, cov);

	EXPECT_GT(cov[K], 0.9f);
	EXPECT_LT(cov[C], 0.15f);
	EXPECT_LT(cov[M], 0.15f);
	EXPECT_LT(cov[Y], 0.15f);
}

// Turning GCR up moves neutral density from the three colour plates onto black
// without changing the colour that gets reproduced.
TEST(CmykSeparationFilter, GcrShiftsNeutralInkOntoBlackPlate)
{
	ColorImage gray = makeSolidColor(4, 4, 110, 110, 110);

	CmykSeparationFilter noBlack;
	noBlack.setParameter("gcr", 0.0f);
	noBlack.setParameter("ink_limit", 4.0f);
	float covNoBlack[4];
	allPlates(noBlack, gray, covNoBlack);

	CmykSeparationFilter fullBlack;
	fullBlack.setParameter("gcr", 1.0f);
	fullBlack.setParameter("ink_limit", 4.0f);
	float covFullBlack[4];
	allPlates(fullBlack, gray, covFullBlack);

	EXPECT_NEAR(covNoBlack[K], 0.0f, 0.01f);
	EXPECT_GT(covFullBlack[K], covNoBlack[K]);
	EXPECT_LT(covFullBlack[C], covNoBlack[C]);
	EXPECT_LT(covFullBlack[M], covNoBlack[M]);
	EXPECT_LT(covFullBlack[Y], covNoBlack[Y]);
}

// The real correctness check: mix known coverages, ask the filter to recover them,
// and it should hand back what went in.
TEST(CmykSeparationFilter, RecoversKnownCoveragesAreaModel)
{
	const float mixes[][4] = {
		{0.60f, 0.20f, 0.40f, 0.0f},
		{0.30f, 0.30f, 0.30f, 0.0f},
		{0.80f, 0.10f, 0.70f, 0.0f}, // heavy cyan + yellow, i.e. a plottable green
		{0.10f, 0.50f, 0.20f, 0.0f},
	};

	for (const auto &mix : mixes)
	{
		ColorImage src = swatchFromCoverage(mix, /*density=*/false);

		CmykSeparationFilter filter;
		filter.setParameter("mixing", 0.0f);      // Area Coverage
		filter.setParameter("ink_profile", 1.0f); // Marker CMY
		filter.setParameter("gcr", 0.0f);
		filter.setParameter("ink_limit", 4.0f);

		float cov[4];
		allPlates(filter, src, cov);

		for (int ink = 0; ink < 3; ++ink)
		{
			EXPECT_NEAR(cov[ink], mix[ink], 0.03f)
				<< "mix(" << mix[0] << "," << mix[1] << "," << mix[2] << ") plate " << ink;
		}
	}
}

TEST(CmykSeparationFilter, RecoversKnownCoveragesDensityModel)
{
	const float mixes[][4] = {
		{0.60f, 0.20f, 0.40f, 0.0f},
		{0.30f, 0.30f, 0.30f, 0.0f},
		{0.50f, 0.10f, 0.45f, 0.0f},
		{0.10f, 0.50f, 0.20f, 0.0f},
	};

	for (const auto &mix : mixes)
	{
		ColorImage src = swatchFromCoverage(mix, /*density=*/true);

		CmykSeparationFilter filter;
		filter.setParameter("mixing", 1.0f);      // Optical Density
		filter.setParameter("ink_profile", 1.0f); // Marker CMY
		filter.setParameter("gcr", 0.0f);
		filter.setParameter("ink_limit", 4.0f);

		float cov[4];
		allPlates(filter, src, cov);

		for (int ink = 0; ink < 3; ++ink)
		{
			EXPECT_NEAR(cov[ink], mix[ink], 0.03f)
				<< "mix(" << mix[0] << "," << mix[1] << "," << mix[2] << ") plate " << ink;
		}
	}
}

// Moving ink onto the black plate must not change the colour that lands on paper.
TEST(CmykSeparationFilter, GcrPreservesReproducedColor)
{
	const float mix[4] = {0.55f, 0.35f, 0.45f, 0.0f};
	ColorImage src = swatchFromCoverage(mix, /*density=*/false);

	float reference[3];
	reconstruct(mix, /*density=*/false, reference);

	for (float gcr : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		CmykSeparationFilter filter;
		filter.setParameter("mixing", 0.0f);
		filter.setParameter("ink_profile", 1.0f);
		filter.setParameter("gcr", gcr);
		filter.setParameter("ink_limit", 4.0f);

		float cov[4];
		allPlates(filter, src, cov);

		float recon[3];
		reconstruct(cov, /*density=*/false, recon);

		for (int ch = 0; ch < 3; ++ch)
			EXPECT_NEAR(recon[ch], reference[ch], 0.02f) << "gcr " << gcr << " channel " << ch;
	}
}

// Ideal block dyes plus the area model collapse to the textbook C = 1 - R, which is
// the sanity anchor for the whole solve.
TEST(CmykSeparationFilter, IdealInksReproduceTextbookSeparation)
{
	ColorImage src = makeSolidColor(2, 2, 64, 128, 192);

	CmykSeparationFilter filter;
	filter.setParameter("ink_profile", 0.0f); // Ideal CMY
	filter.setParameter("mixing", 0.0f);      // Area Coverage
	filter.setParameter("gcr", 0.0f);
	filter.setParameter("ink_limit", 4.0f);

	float cov[4];
	allPlates(filter, src, cov);

	EXPECT_NEAR(cov[C], 1.0f - srgbToLinear(64.0f / 255.0f), 0.02f);
	EXPECT_NEAR(cov[M], 1.0f - srgbToLinear(128.0f / 255.0f), 0.02f);
	EXPECT_NEAR(cov[Y], 1.0f - srgbToLinear(192.0f / 255.0f), 0.02f);
}

// A vivid green is outside what real markers can mix. The solve wants negative
// magenta; the filter has to clamp rather than emit nonsense, and still lay down the
// cyan and yellow that get as close as the inks allow.
TEST(CmykSeparationFilter, OutOfGamutColorsClampInsteadOfGoingNegative)
{
	ColorImage vivid = makeSolidColor(2, 2, 0, 220, 70);

	CmykSeparationFilter filter;
	filter.setParameter("ink_profile", 1.0f);
	filter.setParameter("gcr", 0.0f);
	filter.setParameter("ink_limit", 4.0f);

	float cov[4];
	allPlates(filter, vivid, cov);

	for (int ink = 0; ink < 4; ++ink)
	{
		EXPECT_GE(cov[ink], 0.0f) << "plate " << ink;
		EXPECT_LE(cov[ink], 1.0f) << "plate " << ink;
	}
	EXPECT_NEAR(cov[M], 0.0f, 0.01f) << "magenta would have to be negative";
	EXPECT_GT(cov[C], 0.5f);
	EXPECT_GT(cov[Y], 0.5f);
}

TEST(CmykSeparationFilter, InkLimitCapsTotalCoverage)
{
	ColorImage dark = makeSolidColor(2, 2, 10, 10, 10);

	CmykSeparationFilter filter;
	filter.setParameter("gcr", 0.5f);
	filter.setParameter("ink_limit", 1.5f);

	float cov[4];
	allPlates(filter, dark, cov);

	const float total = cov[C] + cov[M] + cov[Y] + cov[K];
	EXPECT_LE(total, 1.5f + 0.02f);
}

TEST(CmykSeparationFilter, PreservesImageGeometry)
{
	ColorImage src = makeSolidColor(7, 5, 120, 30, 200);
	src.pixel_size_mm = 0.25f;

	CmykSeparationFilter filter;
	Bitmap out;
	filter.applyTyped(src, out);

	EXPECT_EQ(out.width_px, 7u);
	EXPECT_EQ(out.height_px, 5u);
	EXPECT_FLOAT_EQ(out.pixel_size_mm, 0.25f);
	EXPECT_EQ(out.pixels.size(), 35u);
}

TEST(CmykSeparationFilter, EmptyInputProducesEmptyOutput)
{
	ColorImage src;
	CmykSeparationFilter filter;
	Bitmap out;
	filter.applyTyped(src, out);

	EXPECT_EQ(out.width_px, 0u);
	EXPECT_EQ(out.height_px, 0u);
	EXPECT_TRUE(out.pixels.empty());
}
