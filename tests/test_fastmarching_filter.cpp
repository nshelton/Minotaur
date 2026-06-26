#include <gtest/gtest.h>
#include <cmath>
#include "filter_test_helpers.h"
#include "filters/bitmap/FastMarchingFilter.h"

// Constant-brightness image with a centered seed should produce concentric,
// closed iso-rings around the seed (the wave speed is uniform, so travel time
// is radial). Inner rings fit entirely inside the image and must be closed and
// roughly circular.
TEST(FastMarchingFilter, ConstantImageGivesConcentricCircles)
{
	const int n = 101; // odd so the center lands on a pixel
	Bitmap in = makeSolidBitmap(n, n, 128);

	FastMarchingFilter filter;
	filter.setParameter("seedX", 0.5f);
	filter.setParameter("seedY", 0.5f);
	filter.setParameter("levelSpacing", 0.1f);

	PathSet out;
	filter.applyTyped(in, out);

	ASSERT_GE(out.paths.size(), 5u) << "expected several contour rings";

	const Vec2 seed((static_cast<float>(n) * 0.5f), (static_cast<float>(n) * 0.5f));

	int closedCount = 0;
	for (const Path &p : out.paths)
	{
		if (!p.closed) continue;
		++closedCount;

		float minR = 1e9f, maxR = 0.0f, sumR = 0.0f;
		for (const Vec2 &v : p.points)
		{
			float r = std::sqrt((v.x - seed.x) * (v.x - seed.x) +
			                    (v.y - seed.y) * (v.y - seed.y));
			minR = std::min(minR, r);
			maxR = std::max(maxR, r);
			sumR += r;
		}
		float meanR = sumR / static_cast<float>(p.points.size());
		ASSERT_GT(meanR, 0.0f);
		// Tiny inner rings (a few px across) are unavoidably polygonal on the grid;
		// check circularity only where the ring is large enough to be meaningful.
		if (meanR < 15.0f) continue;
		// near-Euclidean FMM: a ring's radius should be nearly constant
		EXPECT_LT((maxR - minR) / meanR, 0.25f)
		    << "closed ring should be roughly circular";
	}
	EXPECT_GE(closedCount, 3) << "inner rings should close into loops";
}

// Brightness must control wave speed: in a half-bright / half-dark image the
// dark (slow) side compresses the rings, so more contour vertices land there
// than on the bright (fast) side.
TEST(FastMarchingFilter, BrightnessControlsRingDensity)
{
	const int n = 101;
	Bitmap in = makeSolidBitmap(n, n, 0);
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x)
			in.pixels[static_cast<size_t>(y) * n + x] = (x < n / 2) ? 220 : 40;

	FastMarchingFilter filter;
	filter.setParameter("levelSpacing", 0.05f);

	PathSet out;
	filter.applyTyped(in, out);
	ASSERT_FALSE(out.paths.empty());

	const float midX = static_cast<float>(n) * 0.5f;
	int brightVerts = 0, darkVerts = 0;
	for (const Path &p : out.paths)
		for (const Vec2 &v : p.points)
			(v.x < midX ? brightVerts : darkVerts)++;

	EXPECT_GT(darkVerts, brightVerts)
	    << "dark/slow side should pack more contour detail than the bright side";
}

TEST(FastMarchingFilter, EmptyImageProducesNoPaths)
{
	Bitmap in; // 0x0
	FastMarchingFilter filter;
	PathSet out;
	filter.applyTyped(in, out);
	EXPECT_TRUE(out.paths.empty());
}
