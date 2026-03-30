#include <gtest/gtest.h>
#include "filter_test_helpers.h"
#include "filters/bitmap/LevelsFilter.h"

TEST(LevelsFilter, IdentitySettingsPreservesInput)
{
	Bitmap in = makeCheckerboard(16, 16, 4);
	Bitmap out;
	LevelsFilter filter;
	// Default: bias=0, gain=1, invert=0 -> identity
	filter.applyTyped(in, out);

	ASSERT_EQ(out.pixels.size(), in.pixels.size());
	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], in.pixels[i]) << "pixel " << i;
	}
}

TEST(LevelsFilter, InvertFlipsValues)
{
	Bitmap in = makeSolidBitmap(8, 8, 0);
	Bitmap out;
	LevelsFilter filter;
	filter.setParameter("invert", 1.0f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 255) << "pixel " << i;
	}
}

TEST(LevelsFilter, InvertWhiteToBlack)
{
	Bitmap in = makeSolidBitmap(8, 8, 255);
	Bitmap out;
	LevelsFilter filter;
	filter.setParameter("invert", 1.0f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 0) << "pixel " << i;
	}
}

TEST(LevelsFilter, GainDoublesValues)
{
	// Input 64 -> normalized 64/255 ~= 0.251
	// gain 2.0 -> 0.502 -> ~128
	Bitmap in = makeSolidBitmap(4, 4, 64);
	Bitmap out;
	LevelsFilter filter;
	filter.setParameter("gain", 2.0f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_NEAR(out.pixels[i], 128, 1) << "pixel " << i;
	}
}

TEST(LevelsFilter, BiasShiftsValues)
{
	// Input 0 -> normalized 0.0
	// bias 0.5 -> 0.5 * gain(1.0) = 0.5 -> 128
	Bitmap in = makeSolidBitmap(4, 4, 0);
	Bitmap out;
	LevelsFilter filter;
	filter.setParameter("bias", 0.5f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_NEAR(out.pixels[i], 128, 1) << "pixel " << i;
	}
}

TEST(LevelsFilter, GainClampsTo255)
{
	// Input 200 -> 200/255 ~= 0.784
	// gain 4.0 -> 3.137 -> clamped to 1.0 -> 255
	Bitmap in = makeSolidBitmap(4, 4, 200);
	Bitmap out;
	LevelsFilter filter;
	filter.setParameter("gain", 4.0f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 255) << "pixel " << i;
	}
}
