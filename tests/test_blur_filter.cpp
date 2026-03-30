#include <gtest/gtest.h>
#include "filter_test_helpers.h"
#include "filters/bitmap/BlurFilter.h"

TEST(BlurFilter, SolidWhiteRemainsWhite)
{
	Bitmap in = makeSolidBitmap(32, 32, 255);
	Bitmap out;
	BlurFilter filter;
	filter.setParameter("radius", 3.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.pixels.size(), in.pixels.size());
	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 255) << "pixel " << i;
	}
}

TEST(BlurFilter, SolidBlackRemainsBlack)
{
	Bitmap in = makeSolidBitmap(32, 32, 0);
	Bitmap out;
	BlurFilter filter;
	filter.setParameter("radius", 3.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.pixels.size(), in.pixels.size());
	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], 0) << "pixel " << i;
	}
}

TEST(BlurFilter, RadiusZeroIsIdentity)
{
	Bitmap in = makeCheckerboard(16, 16, 4);
	Bitmap out;
	BlurFilter filter;
	filter.setParameter("radius", 0.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.pixels.size(), in.pixels.size());
	for (size_t i = 0; i < out.pixels.size(); ++i)
	{
		EXPECT_EQ(out.pixels[i], in.pixels[i]) << "pixel " << i;
	}
}

TEST(BlurFilter, CheckerboardLowersContrast)
{
	// Use a large checkerboard with small blocks so interior pixels are well-mixed
	Bitmap in = makeCheckerboard(64, 64, 2);
	Bitmap out;
	BlurFilter filter;
	filter.setParameter("radius", 3.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.pixels.size(), in.pixels.size());

	// Check interior pixels (away from borders) have reduced contrast
	int radius = 3;
	uint8_t minVal = 255, maxVal = 0;
	for (int y = radius + 2; y < 64 - radius - 2; ++y)
	{
		for (int x = radius + 2; x < 64 - radius - 2; ++x)
		{
			uint8_t v = out.pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)];
			minVal = std::min(minVal, v);
			maxVal = std::max(maxVal, v);
		}
	}
	EXPECT_GT(minVal, 0) << "blur should raise the minimum from 0 in interior";
	EXPECT_LT(maxVal, 255) << "blur should lower the maximum from 255 in interior";
}

TEST(BlurFilter, EmptyInput)
{
	Bitmap in;
	in.width_px = 0;
	in.height_px = 0;
	Bitmap out;
	BlurFilter filter;
	filter.setParameter("radius", 2.0f);
	filter.applyTyped(in, out);

	EXPECT_TRUE(out.pixels.empty());
}

TEST(BlurFilter, PreservesDimensions)
{
	Bitmap in = makeSolidBitmap(50, 30, 128);
	Bitmap out;
	BlurFilter filter;
	filter.setParameter("radius", 2.0f);
	filter.applyTyped(in, out);

	EXPECT_EQ(out.width_px, in.width_px);
	EXPECT_EQ(out.height_px, in.height_px);
	EXPECT_EQ(out.pixel_size_mm, in.pixel_size_mm);
}
