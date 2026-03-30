#include <gtest/gtest.h>
#include "filter_test_helpers.h"
#include "filters/bitmap/TraceFilter.h"

TEST(TraceFilter, SolidRectangleProducesPath)
{
	// Create a small bitmap with a solid black rectangle in the middle
	// TraceFilter: pixels <= 128 are foreground
	Bitmap in = makeSolidBitmap(20, 20, 255);  // white background
	// Fill a 10x10 block in the center with black
	for (int y = 5; y < 15; ++y)
	{
		for (int x = 5; x < 15; ++x)
		{
			in.pixels[static_cast<size_t>(y) * 20 + static_cast<size_t>(x)] = 0;
		}
	}

	PathSet out;
	TraceFilter filter;
	filter.applyTyped(in, out);

	EXPECT_GE(out.paths.size(), 1u) << "should produce at least one path";

	// The traced path should have multiple points
	size_t totalPoints = 0;
	for (const auto &p : out.paths)
	{
		totalPoints += p.points.size();
	}
	EXPECT_GT(totalPoints, 2u) << "traced paths should have multiple points";
}

TEST(TraceFilter, EmptyBitmapProducesNoPaths)
{
	Bitmap in = makeSolidBitmap(16, 16, 255);  // all white = no foreground
	PathSet out;
	TraceFilter filter;
	filter.applyTyped(in, out);

	EXPECT_EQ(out.paths.size(), 0u);
}

TEST(TraceFilter, FullBlackBitmapProducesPaths)
{
	Bitmap in = makeSolidBitmap(10, 10, 0);  // all foreground
	PathSet out;
	TraceFilter filter;
	filter.applyTyped(in, out);

	// All black should produce at least one path
	EXPECT_GE(out.paths.size(), 1u);
}

TEST(TraceFilter, OutputCoordinatesInMillimeters)
{
	Bitmap in = makeSolidBitmap(10, 10, 0, 0.5f);  // pixel_size = 0.5mm
	PathSet out;
	TraceFilter filter;
	filter.applyTyped(in, out);

	ASSERT_GE(out.paths.size(), 1u);
	// All coordinates should be in mm range [0, 10*0.5] = [0, 5]
	for (const auto &path : out.paths)
	{
		for (const auto &pt : path.points)
		{
			EXPECT_GE(pt.x, 0.0f);
			EXPECT_LE(pt.x, 10.0f * 0.5f);
			EXPECT_GE(pt.y, 0.0f);
			EXPECT_LE(pt.y, 10.0f * 0.5f);
		}
	}
}
