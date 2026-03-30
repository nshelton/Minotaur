#include <gtest/gtest.h>

#include "../src/core/Bitmap.h"
#include "../src/core/Pathset.h"
#include "../src/core/FloatImage.h"
#include "../src/filters/Filter.h"
#include "../src/filters/bitmap/ThresholdFilter.h"
#include "../src/filters/bitmap/BlurFilter.h"
#include "../src/filters/bitmap/ErodeDilateFilter.h"
#include "../src/filters/bitmap/RotateFilter.h"
#include "../src/filters/bitmap/TraceFilter.h"
#include "../src/filters/bitmap/BitmapToFloatFilter.h"
#include "../src/filters/bitmap/LineHatchFilter.h"
#include "../src/filters/pathset/SimplifyFilter.h"
#include "../src/filters/pathset/SmoothFilter.h"
#include "../src/filters/pathset/OptimizePathsFilter.h"

// Helper: create a grayscale bitmap with a uniform value
static Bitmap makeUniformBitmap(int w, int h, uint8_t value, float pixel_size_mm = 0.1f)
{
	Bitmap b;
	b.width_px = w;
	b.height_px = h;
	b.pixel_size_mm = pixel_size_mm;
	b.pixels.assign(static_cast<size_t>(w * h), value);
	return b;
}

// Helper: create a bitmap with a horizontal gradient (0 on left, 255 on right)
static Bitmap makeGradientBitmap(int w, int h, float pixel_size_mm = 0.1f)
{
	Bitmap b;
	b.width_px = w;
	b.height_px = h;
	b.pixel_size_mm = pixel_size_mm;
	b.pixels.resize(static_cast<size_t>(w * h));
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			b.pixels[static_cast<size_t>(y * w + x)] = static_cast<uint8_t>(255 * x / (w - 1));
	return b;
}

// Helper: create a simple PathSet with a few paths
static PathSet makeSimplePathSet()
{
	PathSet ps;
	Path p;
	p.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
	p.closed = true;
	ps.paths.push_back(p);

	Path p2;
	p2.points = {{20, 20}, {30, 20}, {30, 30}};
	p2.closed = false;
	ps.paths.push_back(p2);
	return ps;
}

// Helper: apply a filter and return the output LayerPtr
static LayerPtr applyFilter(FilterBase &filter, const LayerPtr &input)
{
	LayerPtr output;
	filter.apply(input, output);
	return output;
}

// ---------------------------------------------------------------------------
// ThresholdFilter tests
// ---------------------------------------------------------------------------

TEST(ThresholdFilter, PixelsBelowMinBecomeBlack)
{
	ThresholdFilter f;
	f.setParameter("min", 100.0f);
	f.setParameter("max", 200.0f);

	auto input = makeUniformBitmap(10, 10, 50); // all pixels below min
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);
	EXPECT_EQ(result.width_px, 10);
	EXPECT_EQ(result.height_px, 10);
	for (auto px : result.pixels)
		EXPECT_EQ(px, 0) << "Pixels below min threshold should be 0";
}

TEST(ThresholdFilter, PixelsInRangeBecomeWhite)
{
	ThresholdFilter f;
	f.setParameter("min", 100.0f);
	f.setParameter("max", 200.0f);

	auto input = makeUniformBitmap(10, 10, 150); // all pixels in range
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);
	for (auto px : result.pixels)
		EXPECT_EQ(px, 255) << "Pixels in [min, max] range should be 255";
}

TEST(ThresholdFilter, PixelsAboveMaxBecomeBlack)
{
	ThresholdFilter f;
	f.setParameter("min", 50.0f);
	f.setParameter("max", 100.0f);

	auto input = makeUniformBitmap(10, 10, 200); // all pixels above max
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);
	for (auto px : result.pixels)
		EXPECT_EQ(px, 0) << "Pixels above max threshold should be 0";
}

TEST(ThresholdFilter, PreservesDimensions)
{
	ThresholdFilter f;
	auto input = makeUniformBitmap(37, 19, 128);
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);
	EXPECT_EQ(result.width_px, 37);
	EXPECT_EQ(result.height_px, 19);
	EXPECT_EQ(result.pixel_size_mm, input.pixel_size_mm);
}

// ---------------------------------------------------------------------------
// BlurFilter tests
// ---------------------------------------------------------------------------

TEST(BlurFilter, UniformImageUnchanged)
{
	BlurFilter f;
	f.setParameter("radius", 3.0f);

	auto input = makeUniformBitmap(20, 20, 128);
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);
	for (auto px : result.pixels)
		EXPECT_NEAR(px, 128, 1) << "Blurring a uniform image should not change pixel values";
}

TEST(BlurFilter, ReducesContrast)
{
	BlurFilter f;
	f.setParameter("radius", 2.0f);

	// Checkerboard pattern: alternating 0 and 255
	Bitmap input;
	input.width_px = 20;
	input.height_px = 20;
	input.pixel_size_mm = 0.1f;
	input.pixels.resize(400);
	for (int y = 0; y < 20; ++y)
		for (int x = 0; x < 20; ++x)
			input.pixels[static_cast<size_t>(y * 20 + x)] = ((x + y) % 2 == 0) ? 255 : 0;

	auto out = applyFilter(f, makeLayerFrom(input));
	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);

	// After blurring, no pixel should be at the extremes
	bool allMiddle = true;
	for (auto px : result.pixels)
	{
		if (px == 0 || px == 255)
			allMiddle = false;
	}
	EXPECT_TRUE(allMiddle) << "Blurring a checkerboard should move all pixels away from extremes";
}

TEST(BlurFilter, PreservesDimensions)
{
	BlurFilter f;
	f.setParameter("radius", 1.0f);

	auto input = makeGradientBitmap(50, 30);
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);
	EXPECT_EQ(result.width_px, 50);
	EXPECT_EQ(result.height_px, 30);
}

// ---------------------------------------------------------------------------
// ErodeDilateFilter tests
// ---------------------------------------------------------------------------

TEST(ErodeDilateFilter, ErodeShrinksWhiteRegion)
{
	ErodeDilateFilter f;
	f.setParameter("operation", 0.0f); // Erode
	f.setParameter("iterations", 1.0f);

	// White square on black background
	auto input = makeUniformBitmap(20, 20, 0);
	for (int y = 5; y < 15; ++y)
		for (int x = 5; x < 15; ++x)
			input.pixels[static_cast<size_t>(y * 20 + x)] = 255;

	auto out = applyFilter(f, makeLayerFrom(input));
	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);

	// After erosion, the border pixels of the white square should be gone
	EXPECT_EQ(result.pixels[5 * 20 + 5], 0) << "Corner pixel should be eroded";
	// Center should still be white
	EXPECT_EQ(result.pixels[10 * 20 + 10], 255) << "Center pixel should survive erosion";
}

TEST(ErodeDilateFilter, DilateExpandsWhiteRegion)
{
	ErodeDilateFilter f;
	f.setParameter("operation", 1.0f); // Dilate
	f.setParameter("iterations", 1.0f);

	// Single white pixel in center of black image
	auto input = makeUniformBitmap(20, 20, 0);
	input.pixels[10 * 20 + 10] = 255;

	auto out = applyFilter(f, makeLayerFrom(input));
	ASSERT_TRUE(isBitmapLayer(out));
	const Bitmap &result = *asBitmapPtr(out);

	// Neighbors of center should now be white
	EXPECT_EQ(result.pixels[10 * 20 + 10], 255) << "Center should remain white";
	EXPECT_EQ(result.pixels[9 * 20 + 10], 255) << "Pixel above should be dilated";
	EXPECT_EQ(result.pixels[11 * 20 + 10], 255) << "Pixel below should be dilated";
	EXPECT_EQ(result.pixels[10 * 20 + 9], 255) << "Pixel left should be dilated";
	EXPECT_EQ(result.pixels[10 * 20 + 11], 255) << "Pixel right should be dilated";
}

// ---------------------------------------------------------------------------
// TraceFilter tests
// ---------------------------------------------------------------------------

TEST(TraceFilter, EmptyImageProducesNoPaths)
{
	TraceFilter f;
	f.setParameter("threshold", 128.0f);

	// All-black image: nothing above threshold
	auto input = makeUniformBitmap(20, 20, 0);
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	EXPECT_TRUE(result.paths.empty()) << "All-black image should produce no traced paths";
}

TEST(TraceFilter, WhiteImageProducesPaths)
{
	TraceFilter f;
	f.setParameter("threshold", 128.0f);

	// All-white image: all above threshold
	auto input = makeUniformBitmap(20, 20, 255);
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	EXPECT_FALSE(result.paths.empty()) << "All-white image should produce traced paths";
}

TEST(TraceFilter, OutputIsPathSet)
{
	TraceFilter f;
	auto input = makeGradientBitmap(30, 30);
	auto out = applyFilter(f, makeLayerFrom(input));
	EXPECT_TRUE(isPathSetLayer(out));
}

// ---------------------------------------------------------------------------
// SimplifyFilter tests
// ---------------------------------------------------------------------------

TEST(SimplifyFilter, ZeroTolerancePreservesPoints)
{
	SimplifyFilter f;
	f.setParameter("toleranceMm", 0.0f);
	f.setParameter("minPathLengthMm", 0.0f);

	auto input = makeSimplePathSet();
	size_t totalPointsBefore = 0;
	for (const auto &p : input.paths)
		totalPointsBefore += p.points.size();

	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	size_t totalPointsAfter = 0;
	for (const auto &p : result.paths)
		totalPointsAfter += p.points.size();

	EXPECT_EQ(result.paths.size(), input.paths.size());
	EXPECT_EQ(totalPointsAfter, totalPointsBefore)
		<< "Zero tolerance should preserve all points";
}

TEST(SimplifyFilter, HighToleranceReducesPoints)
{
	SimplifyFilter f;
	f.setParameter("toleranceMm", 1.0f);
	f.setParameter("minPathLengthMm", 0.0f);

	// Create a path with many collinear-ish points
	PathSet input;
	Path p;
	for (int i = 0; i <= 100; ++i)
	{
		float x = static_cast<float>(i) * 0.5f;
		float y = x + 0.01f * (i % 3); // tiny wobble
		p.points.push_back({x, y});
	}
	p.closed = false;
	input.paths.push_back(p);

	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	ASSERT_EQ(result.paths.size(), 1u);
	EXPECT_LT(result.paths[0].points.size(), p.points.size())
		<< "High tolerance should reduce point count on nearly-collinear path";
}

TEST(SimplifyFilter, MinPathLengthRemovesShortPaths)
{
	SimplifyFilter f;
	f.setParameter("toleranceMm", 0.0f);
	f.setParameter("minPathLengthMm", 5.0f);

	PathSet input;
	// Short path (length ~1.4mm)
	Path short_p;
	short_p.points = {{0, 0}, {1, 1}};
	short_p.closed = false;
	input.paths.push_back(short_p);

	// Long path (length ~30mm)
	Path long_p;
	long_p.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
	long_p.closed = false;
	input.paths.push_back(long_p);

	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	EXPECT_EQ(result.paths.size(), 1u)
		<< "Short path should be removed by minPathLengthMm filter";
}

// ---------------------------------------------------------------------------
// SmoothFilter tests
// ---------------------------------------------------------------------------

TEST(SmoothFilter, ZeroIterationsPreservesPaths)
{
	SmoothFilter f;
	f.setParameter("iterations", 0.0f);

	auto input = makeSimplePathSet();
	auto out = applyFilter(f, makeLayerFrom(input));

	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	ASSERT_EQ(result.paths.size(), input.paths.size());
	for (size_t i = 0; i < input.paths.size(); ++i)
	{
		ASSERT_EQ(result.paths[i].points.size(), input.paths[i].points.size());
		for (size_t j = 0; j < input.paths[i].points.size(); ++j)
		{
			EXPECT_FLOAT_EQ(result.paths[i].points[j].x, input.paths[i].points[j].x);
			EXPECT_FLOAT_EQ(result.paths[i].points[j].y, input.paths[i].points[j].y);
		}
	}
}

TEST(SmoothFilter, SmoothingReducesSharpCorners)
{
	SmoothFilter f;
	f.setParameter("iterations", 5.0f);

	// Zigzag path with sharp corners
	PathSet input;
	Path p;
	p.points = {{0, 0}, {5, 10}, {10, 0}, {15, 10}, {20, 0}};
	p.closed = false;
	input.paths.push_back(p);

	auto out = applyFilter(f, makeLayerFrom(input));
	ASSERT_TRUE(isPathSetLayer(out));
	const PathSet &result = *asPathSetPtr(out);
	ASSERT_EQ(result.paths.size(), 1u);

	// After smoothing, the peak at (5,10) should be less extreme
	// (moved toward the average of its neighbors)
	EXPECT_LT(result.paths[0].points[1].y, 10.0f)
		<< "Smoothing should reduce sharp peaks";
}

// ---------------------------------------------------------------------------
// TODO: Additional filter tests to implement
// ---------------------------------------------------------------------------

// TODO: RotateFilter - verify 0 degree rotation preserves image,
//       180 degree rotation flips pixels, dimensions change for non-square

// TODO: BitmapToFloatFilter - verify output is FloatImage type,
//       dimensions preserved, SDF values have correct sign

// TODO: LineHatchFilter - verify output is PathSet, non-empty for
//       white image, empty for black image, angle parameter affects direction

// TODO: OptimizePathsFilter - verify reorder flag changes path order,
//       merge flag joins nearby endpoints

// TODO: CurlNoiseFilter - verify output PathSet has same path count,
//       points are displaced from original positions

// TODO: LaplacianSmoothFilter - similar to SmoothFilter but different algorithm

// TODO: SkeletonizeFilter - verify thin white lines remain after skeletonization

// TODO: CannyFilter - verify edge detection on gradient image produces edges

// TODO: ClaheFilter - verify contrast-limited histogram equalization
//       produces full-range output

// TODO: FilterChain integration test - chain multiple filters and verify
//       cache invalidation when parameters change
