#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "core/Bitmap.h"
#include "core/Pathset.h"
#include "filters/bitmap/BlurFilter.h"
#include "filters/bitmap/ThresholdFilter.h"
#include "filters/bitmap/LevelsFilter.h"
#include "filters/bitmap/TraceFilter.h"
#include "filters/pathset/SimplifyFilter.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static Bitmap makeSolidBitmap(int w, int h, uint8_t value, float pixel_size_mm = 1.0f)
{
	Bitmap bmp;
	bmp.width_px = static_cast<size_t>(w);
	bmp.height_px = static_cast<size_t>(h);
	bmp.pixel_size_mm = pixel_size_mm;
	bmp.pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), value);
	return bmp;
}

static Bitmap makeCheckerboard(int w, int h, int blockSize, float pixel_size_mm = 1.0f)
{
	Bitmap bmp;
	bmp.width_px = static_cast<size_t>(w);
	bmp.height_px = static_cast<size_t>(h);
	bmp.pixel_size_mm = pixel_size_mm;
	bmp.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			bool white = ((x / blockSize) + (y / blockSize)) % 2 == 0;
			bmp.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = white ? 255 : 0;
		}
	}
	return bmp;
}

static PathSet makeSquarePath(float side)
{
	PathSet ps;
	Path p;
	p.closed = true;
	p.points = {
		Vec2(0.0f, 0.0f),
		Vec2(side, 0.0f),
		Vec2(side, side),
		Vec2(0.0f, side)
	};
	ps.paths.push_back(std::move(p));
	return ps;
}

static PathSet makeCirclePath(float radius, int segments)
{
	PathSet ps;
	Path p;
	p.closed = true;
	for (int i = 0; i < segments; ++i)
	{
		float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(segments);
		p.points.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
	}
	ps.paths.push_back(std::move(p));
	return ps;
}

// ---------------------------------------------------------------------------
// BlurFilter tests
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// ThresholdFilter tests
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// LevelsFilter tests
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// TraceFilter tests
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// SimplifyFilter tests
// ---------------------------------------------------------------------------

TEST(SimplifyFilter, StraightLineReducedToTwoPoints)
{
	PathSet in;
	Path p;
	p.closed = false;
	// 10 collinear points along x-axis
	for (int i = 0; i < 10; ++i)
	{
		p.points.emplace_back(static_cast<float>(i), 0.0f);
	}
	in.paths.push_back(std::move(p));

	PathSet out;
	SimplifyFilter filter;
	filter.setParameter("toleranceMm", 0.1f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.paths.size(), 1u);
	EXPECT_EQ(out.paths[0].points.size(), 2u) << "collinear points should reduce to 2";
	// First and last point preserved
	EXPECT_FLOAT_EQ(out.paths[0].points[0].x, 0.0f);
	EXPECT_FLOAT_EQ(out.paths[0].points[1].x, 9.0f);
}

TEST(SimplifyFilter, CircleWithHighToleranceReducesPoints)
{
	PathSet in = makeCirclePath(10.0f, 64);
	PathSet out;
	SimplifyFilter filter;
	filter.setParameter("toleranceMm", 1.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.paths.size(), 1u);
	EXPECT_LT(out.paths[0].points.size(), 64u) << "high tolerance should reduce points";
	EXPECT_GE(out.paths[0].points.size(), 3u) << "closed path needs at least 3 points";
	EXPECT_TRUE(out.paths[0].closed);
}

TEST(SimplifyFilter, ZeroTolerancePreservesPoints)
{
	PathSet in = makeCirclePath(10.0f, 16);
	PathSet out;
	SimplifyFilter filter;
	filter.setParameter("toleranceMm", 0.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.paths.size(), 1u);
	EXPECT_EQ(out.paths[0].points.size(), in.paths[0].points.size());
}

TEST(SimplifyFilter, MinPathLengthFiltersShortPaths)
{
	PathSet in;
	// Short path (length ~1mm)
	Path short_path;
	short_path.closed = false;
	short_path.points = {Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f)};
	in.paths.push_back(short_path);

	// Long path (length ~100mm)
	Path long_path;
	long_path.closed = false;
	long_path.points = {Vec2(0.0f, 0.0f), Vec2(100.0f, 0.0f)};
	in.paths.push_back(long_path);

	PathSet out;
	SimplifyFilter filter;
	filter.setParameter("minPathLengthMm", 5.0f);
	filter.applyTyped(in, out);

	EXPECT_EQ(out.paths.size(), 1u) << "short path should be filtered out";
	EXPECT_FLOAT_EQ(out.paths[0].points.back().x, 100.0f);
}

TEST(SimplifyFilter, PreservesClosedFlag)
{
	PathSet in = makeSquarePath(10.0f);
	ASSERT_TRUE(in.paths[0].closed);

	PathSet out;
	SimplifyFilter filter;
	filter.setParameter("toleranceMm", 0.0f);
	filter.applyTyped(in, out);

	ASSERT_EQ(out.paths.size(), 1u);
	EXPECT_TRUE(out.paths[0].closed);
}

TEST(SimplifyFilter, EmptyInputProducesEmptyOutput)
{
	PathSet in;
	PathSet out;
	SimplifyFilter filter;
	filter.applyTyped(in, out);

	EXPECT_EQ(out.paths.size(), 0u);
}
