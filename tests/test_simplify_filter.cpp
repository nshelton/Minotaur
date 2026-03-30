#include <gtest/gtest.h>
#include "filter_test_helpers.h"
#include "filters/pathset/SimplifyFilter.h"

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
