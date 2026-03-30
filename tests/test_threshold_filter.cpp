#include <gtest/gtest.h>
#include "filter_test_helpers.h"
#include "filters/bitmap/ThresholdFilter.h"

TEST(ThresholdFilter, KnownInputBinaryOutput)
{
	// Pixels inside [50,150] -> 0, outside -> 255
	Bitmap in = makeSolidBitmap(4, 4, 0);
	// Set up a gradient row: 0, 50, 100, 200
	in.pixels[0] = 0;    // outside below -> 255
	in.pixels[1] = 50;   // inside (boundary) -> 0
	in.pixels[2] = 100;  // inside -> 0
	in.pixels[3] = 200;  // outside above -> 255

	Bitmap out;
	ThresholdFilter filter;
	filter.setParameter("min", 50.0f);
	filter.setParameter("max", 150.0f);
	filter.applyTyped(in, out);

	EXPECT_EQ(out.pixels[0], 255);  // 0 is outside [50,150]
	EXPECT_EQ(out.pixels[1], 0);    // 50 is inside
	EXPECT_EQ(out.pixels[2], 0);    // 100 is inside
	EXPECT_EQ(out.pixels[3], 255);  // 200 is outside
}

TEST(ThresholdFilter, AllInsideRange)
{
	Bitmap in = makeSolidBitmap(8, 8, 128);
	Bitmap out;
	ThresholdFilter filter;
	filter.setParameter("min", 0.0f);
	filter.setParameter("max", 255.0f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 0) << "pixel " << i;
	}
}

TEST(ThresholdFilter, AllOutsideRange)
{
	Bitmap in = makeSolidBitmap(8, 8, 200);
	Bitmap out;
	ThresholdFilter filter;
	filter.setParameter("min", 50.0f);
	filter.setParameter("max", 100.0f);
	filter.applyTyped(in, out);

	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 255) << "pixel " << i;
	}
}

TEST(ThresholdFilter, SwappedMinMaxNormalized)
{
	// If min > max, filter swaps them
	Bitmap in = makeSolidBitmap(4, 4, 100);
	Bitmap out;
	ThresholdFilter filter;
	filter.setParameter("min", 150.0f);
	filter.setParameter("max", 50.0f);
	filter.applyTyped(in, out);

	// 100 is inside [50,150] after swap -> 0
	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 0) << "pixel " << i;
	}
}
