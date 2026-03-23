#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>
#include <vector>
#include "../src/utils/KdTree2D.h"

TEST(KdTree2D, InsertAndNearest)
{
	KdTree2D tree;
	tree.insert(Vec2(1.0f, 1.0f), 0);
	tree.insert(Vec2(5.0f, 5.0f), 1);
	tree.insert(Vec2(9.0f, 9.0f), 2);

	auto [key, dist2] = tree.nearest(Vec2(1.1f, 1.1f));
	EXPECT_EQ(key, 0);
	EXPECT_LT(dist2, 0.1f);
}

TEST(KdTree2D, NearestAmongMany)
{
	KdTree2D tree;
	// Insert a grid of points
	int id = 0;
	for (int x = 0; x < 10; ++x)
	{
		for (int y = 0; y < 10; ++y)
		{
			tree.insert(Vec2(static_cast<float>(x), static_cast<float>(y)), id++);
		}
	}

	// Query near (3.1, 7.2) — nearest should be (3, 7) which is id = 3*10 + 7 = 37
	auto [key, dist2] = tree.nearest(Vec2(3.1f, 7.2f));
	EXPECT_EQ(key, 37);
	EXPECT_LT(dist2, 0.1f);
}

TEST(KdTree2D, Remove)
{
	KdTree2D tree;
	tree.insert(Vec2(0.0f, 0.0f), 0);
	tree.insert(Vec2(10.0f, 10.0f), 1);
	tree.insert(Vec2(5.0f, 5.0f), 2);

	// Nearest to origin is key 0
	auto [key1, d1] = tree.nearest(Vec2(0.1f, 0.1f));
	EXPECT_EQ(key1, 0);

	// Remove key 0, nearest to origin should now be key 2
	EXPECT_TRUE(tree.remove(0));
	auto [key2, d2] = tree.nearest(Vec2(0.1f, 0.1f));
	EXPECT_EQ(key2, 2);
}

TEST(KdTree2D, RemoveNonexistent)
{
	KdTree2D tree;
	tree.insert(Vec2(1.0f, 1.0f), 0);
	EXPECT_FALSE(tree.remove(99));
}

TEST(KdTree2D, EmptyTree)
{
	KdTree2D tree;
	auto [key, dist2] = tree.nearest(Vec2(0.0f, 0.0f));
	EXPECT_EQ(key, -1);
	EXPECT_TRUE(std::isinf(dist2));
}

TEST(KdTree2D, Clear)
{
	KdTree2D tree;
	tree.insert(Vec2(1.0f, 1.0f), 0);
	tree.insert(Vec2(2.0f, 2.0f), 1);
	tree.clear();

	auto [key, dist2] = tree.nearest(Vec2(1.0f, 1.0f));
	EXPECT_EQ(key, -1);
}

TEST(KdTree2D, ManyRandomPoints)
{
	KdTree2D tree;
	std::srand(42);
	const int N = 1000;
	std::vector<Vec2> points(N);

	for (int i = 0; i < N; ++i)
	{
		points[i] = Vec2(
			10.0f * static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX),
			10.0f * static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
		tree.insert(points[i], i);
	}

	// Verify nearest by brute force for several queries
	Vec2 queries[] = {Vec2(5.0f, 5.0f), Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f), Vec2(3.7f, 8.2f)};
	for (const auto &q : queries)
	{
		auto [treeKey, treeDist2] = tree.nearest(q);

		// Brute force
		int bestKey = -1;
		float bestD2 = std::numeric_limits<float>::infinity();
		for (int i = 0; i < N; ++i)
		{
			float dx = points[i].x - q.x;
			float dy = points[i].y - q.y;
			float d2 = dx * dx + dy * dy;
			if (d2 < bestD2)
			{
				bestD2 = d2;
				bestKey = i;
			}
		}

		EXPECT_EQ(treeKey, bestKey) << "Query: (" << q.x << ", " << q.y << ")";
		EXPECT_NEAR(treeDist2, bestD2, 1e-6f);
	}
}
