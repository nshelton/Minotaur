#include <gtest/gtest.h>
#include <glog/logging.h>
#include <cmath>

#include "generators/GeneratorTyped.h"
#include "generators/pathset/CircleGenerator.h"
#include "generators/pathset/SquareGenerator.h"
#include "generators/pathset/StarGenerator.h"
#include "core/entity.h"

// ===========================================================================
// CircleGenerator Tests
// ===========================================================================

TEST(CircleGenerator, ProducesValidPathSet)
{
	CircleGenerator gen;
	PathSet out;
	gen.generateTyped(out);

	EXPECT_EQ(out.paths.size(), 1u);
	EXPECT_TRUE(out.paths[0].closed);
	EXPECT_GT(out.paths[0].points.size(), 2u);
}

TEST(CircleGenerator, DefaultRadiusIs50)
{
	CircleGenerator gen;
	PathSet out;
	gen.generateTyped(out);

	// All points should be approximately 50mm from origin
	for (const auto &pt : out.paths[0].points)
	{
		float dist = std::sqrt(pt.x * pt.x + pt.y * pt.y);
		EXPECT_NEAR(dist, 50.0f, 0.1f);
	}
}

TEST(CircleGenerator, RadiusParameterAffectsOutput)
{
	CircleGenerator gen;
	gen.setParameter("radius_mm", 100.0f);

	PathSet out;
	gen.generateTyped(out);

	for (const auto &pt : out.paths[0].points)
	{
		float dist = std::sqrt(pt.x * pt.x + pt.y * pt.y);
		EXPECT_NEAR(dist, 100.0f, 0.1f);
	}
}

TEST(CircleGenerator, SegmentsParameterAffectsPointCount)
{
	CircleGenerator gen;
	gen.setParameter("segments", 12.0f);

	PathSet out;
	gen.generateTyped(out);

	EXPECT_EQ(out.paths[0].points.size(), 12u);
}

TEST(CircleGenerator, MinimumSegmentsClampedTo3)
{
	CircleGenerator gen;
	gen.setParameter("segments", 1.0f);

	PathSet out;
	gen.generateTyped(out);

	EXPECT_GE(out.paths[0].points.size(), 3u);
}

TEST(CircleGenerator, OutputKindIsPathSet)
{
	CircleGenerator gen;
	EXPECT_EQ(gen.outputKind(), LayerKind::PathSet);
}

TEST(CircleGenerator, NameIsCircle)
{
	CircleGenerator gen;
	EXPECT_STREQ(gen.name(), "Circle");
}

// ===========================================================================
// Generator Parameter Versioning
// ===========================================================================

TEST(GeneratorVersioning, VersionIncreasesOnSetParameter)
{
	CircleGenerator gen;
	uint64_t v1 = gen.paramVersion();

	gen.setParameter("radius_mm", 75.0f);
	uint64_t v2 = gen.paramVersion();

	EXPECT_GT(v2, v1);
}

TEST(GeneratorVersioning, VersionDoesNotChangeForInvalidKey)
{
	CircleGenerator gen;
	uint64_t v1 = gen.paramVersion();

	gen.setParameter("nonexistent_param", 42.0f);
	uint64_t v2 = gen.paramVersion();

	EXPECT_EQ(v2, v1);
}

TEST(GeneratorVersioning, MultipleSetParameterCallsIncrement)
{
	CircleGenerator gen;
	uint64_t v1 = gen.paramVersion();

	gen.setParameter("radius_mm", 10.0f);
	gen.setParameter("radius_mm", 20.0f);
	gen.setParameter("segments", 32.0f);
	uint64_t v2 = gen.paramVersion();

	EXPECT_EQ(v2, v1 + 3);
}

// ===========================================================================
// Generator Clone
// ===========================================================================

TEST(GeneratorClone, CloneCopiesParameters)
{
	CircleGenerator gen;
	gen.setParameter("radius_mm", 123.0f);
	gen.setParameter("segments", 42.0f);

	auto cloned = gen.clone();
	ASSERT_NE(cloned, nullptr);

	EXPECT_STREQ(cloned->name(), "Circle");
	EXPECT_FLOAT_EQ(cloned->m_parameters.at("radius_mm").value, 123.0f);
	EXPECT_FLOAT_EQ(cloned->m_parameters.at("segments").value, 42.0f);
}

TEST(GeneratorClone, CloneIsIndependent)
{
	CircleGenerator gen;
	gen.setParameter("radius_mm", 50.0f);

	auto cloned = gen.clone();
	// Modifying clone should not affect original
	cloned->setParameter("radius_mm", 99.0f);

	EXPECT_FLOAT_EQ(gen.m_parameters.at("radius_mm").value, 50.0f);
	EXPECT_FLOAT_EQ(cloned->m_parameters.at("radius_mm").value, 99.0f);
}

// ===========================================================================
// Generator via generate() (LayerPtr interface)
// ===========================================================================

TEST(GeneratorGenerate, ProducesLayerPtr)
{
	CircleGenerator gen;
	LayerPtr out;
	gen.generate(out);

	ASSERT_NE(out, nullptr);
	EXPECT_EQ(out->kind(), LayerKind::PathSet);

	const auto *ps = static_cast<const PathSet *>(out.get());
	EXPECT_EQ(ps->paths.size(), 1u);
}

// ===========================================================================
// SquareGenerator Tests
// ===========================================================================

TEST(SquareGenerator, ProducesClosedSquare)
{
	SquareGenerator gen;
	PathSet out;
	gen.generateTyped(out);

	ASSERT_EQ(out.paths.size(), 1u);
	EXPECT_TRUE(out.paths[0].closed);
	EXPECT_EQ(out.paths[0].points.size(), 4u);
}

TEST(SquareGenerator, SizeParameterWorks)
{
	SquareGenerator gen;
	gen.setParameter("side_mm", 100.0f);

	PathSet out;
	gen.generateTyped(out);

	// Check that the square spans 100mm
	float minX = 1e9f, maxX = -1e9f;
	for (const auto &pt : out.paths[0].points)
	{
		minX = std::min(minX, pt.x);
		maxX = std::max(maxX, pt.x);
	}
	EXPECT_NEAR(maxX - minX, 100.0f, 0.1f);
}

// ===========================================================================
// StarGenerator Tests
// ===========================================================================

TEST(StarGenerator, ProducesClosedPath)
{
	StarGenerator gen;
	PathSet out;
	gen.generateTyped(out);

	ASSERT_EQ(out.paths.size(), 1u);
	EXPECT_TRUE(out.paths[0].closed);
	EXPECT_GT(out.paths[0].points.size(), 4u);
}

// ===========================================================================
// Entity + Generator Integration
// ===========================================================================

TEST(EntityGenerator, TickGeneratorPopulatesPayload)
{
	Entity e;
	e.id = 0;
	e.name = "test";
	e.payload = std::make_shared<PathSet>();
	e.generator = std::make_unique<CircleGenerator>();

	e.tickGenerator();

	ASSERT_NE(e.payload, nullptr);
	EXPECT_EQ(e.payload->kind(), LayerKind::PathSet);
	const auto *ps = static_cast<const PathSet *>(e.payload.get());
	EXPECT_GT(ps->paths.size(), 0u);
}

TEST(EntityGenerator, TickGeneratorIncrementsPayloadVersion)
{
	Entity e;
	e.id = 0;
	e.name = "test";
	e.payload = std::make_shared<PathSet>();
	e.generator = std::make_unique<CircleGenerator>();

	uint64_t v1 = e.payloadVersion;
	e.tickGenerator();
	uint64_t v2 = e.payloadVersion;

	EXPECT_GT(v2, v1);
}

TEST(EntityGenerator, TickGeneratorSkipsIfVersionUnchanged)
{
	Entity e;
	e.id = 0;
	e.name = "test";
	e.payload = std::make_shared<PathSet>();
	e.generator = std::make_unique<CircleGenerator>();

	e.tickGenerator();
	uint64_t v1 = e.payloadVersion;

	// Tick again without changing params
	e.tickGenerator();
	uint64_t v2 = e.payloadVersion;

	EXPECT_EQ(v2, v1);
}

TEST(EntityGenerator, TickGeneratorRerunsAfterParamChange)
{
	Entity e;
	e.id = 0;
	e.name = "test";
	e.payload = std::make_shared<PathSet>();
	e.generator = std::make_unique<CircleGenerator>();

	e.tickGenerator();
	uint64_t v1 = e.payloadVersion;

	e.generator->setParameter("radius_mm", 200.0f);
	e.tickGenerator();
	uint64_t v2 = e.payloadVersion;

	EXPECT_GT(v2, v1);

	const auto *ps = static_cast<const PathSet *>(e.payload.get());
	float dist = std::sqrt(ps->paths[0].points[0].x * ps->paths[0].points[0].x +
	                        ps->paths[0].points[0].y * ps->paths[0].points[0].y);
	EXPECT_NEAR(dist, 200.0f, 0.1f);
}

TEST(EntityGenerator, EntityWithoutGeneratorTickIsNoop)
{
	Entity e;
	e.id = 0;
	e.name = "test";
	e.payload = std::make_shared<PathSet>();
	// No generator

	uint64_t v1 = e.payloadVersion;
	e.tickGenerator();
	EXPECT_EQ(e.payloadVersion, v1);
}

// ===========================================================================
// Entity + FilterChain Integration
// ===========================================================================

TEST(EntityFilterChain, GeneratorFeedsFilterChain)
{
	Entity e;
	e.id = 0;
	e.name = "test";
	e.payload = std::make_shared<PathSet>();
	e.generator = std::make_unique<CircleGenerator>();

	e.tickGenerator();
	e.refreshFilterBase();

	// Verify filter chain base is set
	auto output = e.filterChain.outputBlocking();
	ASSERT_NE(output, nullptr);
	EXPECT_EQ(output->kind(), LayerKind::PathSet);
}

int main(int argc, char **argv)
{
	google::InitGoogleLogging(argv[0]);
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
