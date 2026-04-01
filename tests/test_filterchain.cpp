#include <gtest/gtest.h>
#include <glog/logging.h>
#include <thread>
#include <chrono>

#include "filters/FilterChain.h"
#include "filters/Filter.h"
#include "core/Core.h"
#include "filter_test_helpers.h"

// ---------------------------------------------------------------------------
// Mock filters for testing
// ---------------------------------------------------------------------------

// Identity filter: Bitmap -> Bitmap (copies input unchanged)
struct IdentityBitmapFilter : public FilterTyped<Bitmap, Bitmap>
{
	const char *name() const override { return "IdentityBitmap"; }
	uint64_t paramVersion() const override { return m_version.load(); }
	void applyTyped(const Bitmap &in, Bitmap &out) const override
	{
		out = in;
	}
};

// Invert filter: Bitmap -> Bitmap (inverts pixel values)
struct InvertBitmapFilter : public FilterTyped<Bitmap, Bitmap>
{
	InvertBitmapFilter()
	{
		m_parameters["strength"] = FilterParameter{"strength", 0.0f, 1.0f, 1.0f};
	}
	const char *name() const override { return "InvertBitmap"; }
	uint64_t paramVersion() const override { return m_version.load(); }
	void applyTyped(const Bitmap &in, Bitmap &out) const override
	{
		out.width_px = in.width_px;
		out.height_px = in.height_px;
		out.pixel_size_mm = in.pixel_size_mm;
		out.pixels.resize(in.pixels.size());
		for (size_t i = 0; i < in.pixels.size(); ++i)
		{
			out.pixels[i] = static_cast<uint8_t>(255 - in.pixels[i]);
		}
	}
};

// Count filter: Bitmap -> PathSet (creates a single path with point count = pixel count)
struct BitmapToPathFilter : public FilterTyped<Bitmap, PathSet>
{
	const char *name() const override { return "BitmapToPath"; }
	uint64_t paramVersion() const override { return m_version.load(); }
	void applyTyped(const Bitmap &in, PathSet &out) const override
	{
		out.paths.clear();
		Path p;
		p.closed = false;
		p.points.push_back(Vec2{0.0f, 0.0f});
		p.points.push_back(Vec2{static_cast<float>(in.width_px), static_cast<float>(in.height_px)});
		out.paths.push_back(std::move(p));
	}
};

// PathSet -> PathSet identity
struct IdentityPathSetFilter : public FilterTyped<PathSet, PathSet>
{
	const char *name() const override { return "IdentityPathSet"; }
	uint64_t paramVersion() const override { return m_version.load(); }
	void applyTyped(const PathSet &in, PathSet &out) const override
	{
		out = in;
	}
};

// Slow filter for async testing: Bitmap -> Bitmap with a controllable delay
struct SlowBitmapFilter : public FilterTyped<Bitmap, Bitmap>
{
	int delayMs{50};
	const char *name() const override { return "SlowBitmap"; }
	uint64_t paramVersion() const override { return m_version.load(); }
	void applyTyped(const Bitmap &in, Bitmap &out) const override
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		out = in;
	}
};

// ---------------------------------------------------------------------------
// Helper to create a base layer from a Bitmap
// ---------------------------------------------------------------------------
static LayerPtr makeBitmapLayer(int w, int h, uint8_t value)
{
	auto bmp = std::make_shared<Bitmap>();
	bmp->width_px = static_cast<size_t>(w);
	bmp->height_px = static_cast<size_t>(h);
	bmp->pixel_size_mm = 1.0f;
	bmp->pixels.assign(static_cast<size_t>(w * h), value);
	return bmp;
}

static LayerPtr makePathSetLayer()
{
	auto ps = std::make_shared<PathSet>();
	Path p;
	p.closed = true;
	p.points = {Vec2{0, 0}, Vec2{10, 0}, Vec2{10, 10}, Vec2{0, 10}};
	ps->paths.push_back(std::move(p));
	return ps;
}

// ===========================================================================
// FilterChain Tests
// ===========================================================================

// ---- Basic construction & empty chain ----

TEST(FilterChain, EmptyChainReturnsBase)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	EXPECT_EQ(chain.size(), 0u);
	EXPECT_EQ(chain.output(), base);
	EXPECT_EQ(chain.outputBlocking(), base);
}

TEST(FilterChain, EmptyChainWithNoBaseReturnsNull)
{
	FilterChain chain;
	EXPECT_EQ(chain.output(), nullptr);
	EXPECT_EQ(chain.outputBlocking(), nullptr);
}

// ---- Adding filters ----

TEST(FilterChain, AddSingleFilter)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	auto idx = chain.addFilter(std::make_unique<IdentityBitmapFilter>());
	EXPECT_EQ(idx, 0u);
	EXPECT_EQ(chain.size(), 1u);
	EXPECT_EQ(chain.filterCount(), 1u);
}

TEST(FilterChain, AddMultipleFiltersInChain)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<IdentityBitmapFilter>());
	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	EXPECT_EQ(chain.size(), 2u);
}

TEST(FilterChain, AddFilterWithTypeConversion)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<BitmapToPathFilter>());
	EXPECT_EQ(chain.size(), 1u);

	// Now we can add a PathSet filter after it
	chain.addFilter(std::make_unique<IdentityPathSetFilter>());
	EXPECT_EQ(chain.size(), 2u);
}

// ---- Blocking output / evaluation ----

TEST(FilterChain, BlockingOutputEvaluatesChain)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 200);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	auto result = chain.outputBlocking();

	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result->kind(), LayerKind::Bitmap);
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 55); // 255 - 200
}

TEST(FilterChain, ChainedFiltersEvaluateInOrder)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 100);
	chain.setBase(base, 1);

	// Invert: 100 -> 155
	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	// Invert again: 155 -> 100
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	auto result = chain.outputBlocking();
	ASSERT_NE(result, nullptr);
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 100); // double invert = identity
}

TEST(FilterChain, TypeConversionChain)
{
	FilterChain chain;
	auto base = makeBitmapLayer(8, 6, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<BitmapToPathFilter>());
	auto result = chain.outputBlocking();

	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result->kind(), LayerKind::PathSet);
	const auto &ps = *static_cast<const PathSet *>(result.get());
	ASSERT_EQ(ps.paths.size(), 1u);
	EXPECT_FLOAT_EQ(ps.paths[0].points[1].x, 8.0f);
	EXPECT_FLOAT_EQ(ps.paths[0].points[1].y, 6.0f);
}

// ---- Caching ----

TEST(FilterChain, CacheIsValidAfterFirstEval)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	// First eval
	chain.outputBlocking();
	EXPECT_FALSE(chain.needsEval());

	// Second call should use cache
	auto result = chain.outputBlocking();
	ASSERT_NE(result, nullptr);
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 127); // 255 - 128
}

TEST(FilterChain, CacheInvalidatedByParameterChange)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	auto filter = std::make_unique<InvertBitmapFilter>();
	auto *filterPtr = filter.get();
	chain.addFilter(std::move(filter));

	chain.outputBlocking();
	EXPECT_FALSE(chain.needsEval());

	// Change parameter -> should invalidate
	filterPtr->setParameter("strength", 0.5f);
	EXPECT_TRUE(chain.needsEval());
}

TEST(FilterChain, CacheInvalidatedByBaseChange)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.outputBlocking();
	EXPECT_FALSE(chain.needsEval());

	// Change base -> should invalidate
	auto newBase = makeBitmapLayer(4, 4, 200);
	chain.setBase(newBase, 2);
	EXPECT_TRUE(chain.needsEval());

	auto result = chain.outputBlocking();
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 55); // 255 - 200
}

TEST(FilterChain, InvalidateAllForcesCacheRebuild)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.outputBlocking();
	EXPECT_FALSE(chain.needsEval());

	chain.invalidateAll();
	EXPECT_TRUE(chain.needsEval());
}

TEST(FilterChain, DownstreamCacheInvalidatedWhenUpstreamChanges)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 100);
	chain.setBase(base, 1);

	auto f1 = std::make_unique<InvertBitmapFilter>();
	auto *f1Ptr = f1.get();
	chain.addFilter(std::move(f1));
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	chain.outputBlocking();
	EXPECT_FALSE(chain.needsEval());

	// Change first filter's parameter -> both caches should invalidate
	f1Ptr->setParameter("strength", 0.5f);
	EXPECT_TRUE(chain.needsEval());
}

// ---- Enable / Disable ----

TEST(FilterChain, DisableFilterBypassesIt)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 100);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.setFilterEnabled(0, false);

	auto result = chain.outputBlocking();
	ASSERT_NE(result, nullptr);
	// Disabled invert -> should pass base through
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 100);
}

TEST(FilterChain, ReenableFilter)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 100);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.setFilterEnabled(0, false);
	chain.outputBlocking();

	chain.setFilterEnabled(0, true);
	auto result = chain.outputBlocking();
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 155); // 255 - 100
}

TEST(FilterChain, DisableMiddleFilterInChain)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 100);
	chain.setBase(base, 1);

	// Invert: 100 -> 155
	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	// Identity (will be disabled)
	chain.addFilter(std::make_unique<IdentityBitmapFilter>());
	// Invert: 155 -> 100 (if middle enabled) or 155 -> 100 (middle disabled, same result here)
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	// With middle enabled: 100 -> 155 -> 155 -> 100
	auto r1 = chain.outputBlocking();
	EXPECT_EQ(static_cast<const Bitmap *>(r1.get())->pixels[0], 100);

	// Disable middle: 100 -> 155 -> (bypass) -> 100
	chain.setFilterEnabled(1, false);
	auto r2 = chain.outputBlocking();
	EXPECT_EQ(static_cast<const Bitmap *>(r2.get())->pixels[0], 100);
}

TEST(FilterChain, CannotDisableFilterIfTypeBreaks)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	// Bitmap -> PathSet
	chain.addFilter(std::make_unique<BitmapToPathFilter>());
	// PathSet -> PathSet
	chain.addFilter(std::make_unique<IdentityPathSetFilter>());

	// Cannot disable first filter: base is Bitmap but second filter expects PathSet
	EXPECT_FALSE(chain.canDisableFilterAtIndex(0));

	// Can disable second filter: first filter outputs PathSet, no downstream
	EXPECT_TRUE(chain.canDisableFilterAtIndex(1));
}

TEST(FilterChain, IsFilterEnabled)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<IdentityBitmapFilter>());
	EXPECT_TRUE(chain.isFilterEnabled(0));

	chain.setFilterEnabled(0, false);
	EXPECT_FALSE(chain.isFilterEnabled(0));
}

// ---- Filter removal ----

TEST(FilterChain, RemoveOnlyFilter)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	EXPECT_EQ(chain.size(), 1u);

	EXPECT_TRUE(chain.removeFilter(0));
	EXPECT_EQ(chain.size(), 0u);

	auto result = chain.outputBlocking();
	EXPECT_EQ(result, base);
}

TEST(FilterChain, RemoveLastFilter)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 100);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.addFilter(std::make_unique<IdentityBitmapFilter>());

	EXPECT_TRUE(chain.removeFilter(1));
	EXPECT_EQ(chain.size(), 1u);

	auto result = chain.outputBlocking();
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 155);
}

TEST(FilterChain, CannotRemoveFilterIfTypeBreaks)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	// Bitmap -> PathSet
	chain.addFilter(std::make_unique<BitmapToPathFilter>());
	// PathSet -> PathSet
	chain.addFilter(std::make_unique<IdentityPathSetFilter>());

	// Cannot remove first: base is Bitmap but second expects PathSet
	EXPECT_FALSE(chain.canRemoveFilterAtIndex(0));
	EXPECT_FALSE(chain.removeFilter(0));
	EXPECT_EQ(chain.size(), 2u);
}

TEST(FilterChain, RemoveOutOfBoundsReturnsFalse)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<IdentityBitmapFilter>());
	EXPECT_FALSE(chain.removeFilter(5));
}

// ---- Type validation ----

TEST(FilterChain, CanEnableFilterTypeCheck)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	// Bitmap -> PathSet
	chain.addFilter(std::make_unique<BitmapToPathFilter>());
	// PathSet -> PathSet (initially enabled)
	chain.addFilter(std::make_unique<IdentityPathSetFilter>());

	// First filter: base is Bitmap, filter accepts Bitmap -> yes can enable
	EXPECT_TRUE(chain.canEnableFilterAtIndex(0));
	// Second filter: first filter outputs PathSet, this accepts PathSet -> yes
	EXPECT_TRUE(chain.canEnableFilterAtIndex(1));
}

TEST(FilterChain, BaseKindReflectsBase)
{
	FilterChain chain;
	auto bmpBase = makeBitmapLayer(4, 4, 128);
	chain.setBase(bmpBase, 1);
	EXPECT_EQ(chain.baseKind(), LayerKind::Bitmap);

	auto psBase = makePathSetLayer();
	chain.setBase(psBase, 2);
	EXPECT_EQ(chain.baseKind(), LayerKind::PathSet);
}

// ---- Async / non-blocking evaluation ----

TEST(FilterChain, NonBlockingOutputKicksOffEval)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	auto sf = std::make_unique<SlowBitmapFilter>();
	sf->delayMs = 100;
	chain.addFilter(std::move(sf));

	// Non-blocking output should return base (no result yet)
	auto result = chain.output();
	// Either returns base or a previous result
	ASSERT_NE(result, nullptr);

	// Wait for evaluation to complete
	chain.waitForEval();

	// Now blocking output should give the evaluated result
	auto final_result = chain.outputBlocking();
	ASSERT_NE(final_result, nullptr);
	EXPECT_EQ(final_result->kind(), LayerKind::Bitmap);
}

TEST(FilterChain, WaitForEvalCompletesBackground)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	auto sf = std::make_unique<SlowBitmapFilter>();
	sf->delayMs = 50;
	chain.addFilter(std::move(sf));

	// Trigger background eval
	chain.output();
	// Wait should block until done
	chain.waitForEval();
	EXPECT_FALSE(chain.isEvaluating());
}

TEST(FilterChain, IsEvaluatingDuringBackgroundWork)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	auto sf = std::make_unique<SlowBitmapFilter>();
	sf->delayMs = 200;
	chain.addFilter(std::move(sf));

	// Trigger evaluation
	chain.output();
	// Give the worker a moment to start
	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	// Should be evaluating
	EXPECT_TRUE(chain.isEvaluating());

	chain.waitForEval();
	EXPECT_FALSE(chain.isEvaluating());
}

// ---- Generation tracking ----

TEST(FilterChain, BaseGenTracksVersion)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	EXPECT_EQ(chain.baseGen(), 1u);

	auto base2 = makeBitmapLayer(4, 4, 200);
	chain.setBase(base2, 42);
	EXPECT_EQ(chain.baseGen(), 42u);
}

TEST(FilterChain, LayerCacheGenAdvancesOnRecompute)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	chain.outputBlocking();
	auto cache1 = chain.layerCacheAt(0);
	EXPECT_TRUE(cache1.valid);
	uint64_t gen1 = cache1.gen;

	// Change base -> recompute
	auto base2 = makeBitmapLayer(4, 4, 200);
	chain.setBase(base2, 2);
	chain.outputBlocking();
	auto cache2 = chain.layerCacheAt(0);
	EXPECT_TRUE(cache2.valid);
	EXPECT_GT(cache2.gen, gen1);
}

// ---- Clear ----

TEST(FilterChain, ClearResetsEverything)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.outputBlocking();

	chain.clear();
	EXPECT_EQ(chain.size(), 0u);
	EXPECT_EQ(chain.baseGen(), 0u);
	EXPECT_EQ(chain.output(), nullptr);
}

// ---- Copy semantics ----

TEST(FilterChain, CopyDoesNotCopyFilters)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	FilterChain copy(chain);
	EXPECT_EQ(copy.size(), 0u);        // Filters NOT copied
	EXPECT_EQ(copy.baseGen(), 1u);      // Base gen IS copied
}

// ---- Move semantics ----

TEST(FilterChain, MoveTransfersFilters)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.outputBlocking();

	FilterChain moved(std::move(chain));
	EXPECT_EQ(moved.size(), 1u);

	auto result = moved.outputBlocking();
	ASSERT_NE(result, nullptr);
	const auto &bmp = *static_cast<const Bitmap *>(result.get());
	EXPECT_EQ(bmp.pixels[0], 127); // 255 - 128
}

// ---- Filter accessor ----

TEST(FilterChain, FilterAtReturnsCorrectFilter)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	chain.addFilter(std::make_unique<InvertBitmapFilter>());
	chain.addFilter(std::make_unique<IdentityBitmapFilter>());

	EXPECT_STREQ(chain.filterAt(0)->name(), "InvertBitmap");
	EXPECT_STREQ(chain.filterAt(1)->name(), "IdentityBitmap");
	EXPECT_EQ(chain.filterAt(5), nullptr);
}

// ---- OutputLayer ----

TEST(FilterChain, OutputLayerReturnsNullBeforeFirstEval)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	// Before any eval, outputLayer() should return null
	auto layer = chain.outputLayer();
	EXPECT_EQ(layer, nullptr);
}

TEST(FilterChain, OutputLayerReturnsResultAfterEval)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);
	chain.addFilter(std::make_unique<InvertBitmapFilter>());

	chain.outputBlocking();
	auto layer = chain.outputLayer();
	ASSERT_NE(layer, nullptr);
	EXPECT_EQ(layer->kind(), LayerKind::Bitmap);
}

// ---- needsEval skips disabled filters ----

TEST(FilterChain, NeedsEvalSkipsDisabledFilters)
{
	FilterChain chain;
	auto base = makeBitmapLayer(4, 4, 128);
	chain.setBase(base, 1);

	auto f = std::make_unique<InvertBitmapFilter>();
	auto *fPtr = f.get();
	chain.addFilter(std::move(f));
	chain.setFilterEnabled(0, false);

	chain.outputBlocking();
	EXPECT_FALSE(chain.needsEval());

	// Changing parameter on a disabled filter should not require eval
	// (because needsEval skips disabled filters)
	fPtr->setParameter("strength", 0.5f);
	EXPECT_FALSE(chain.needsEval());
}

int main(int argc, char **argv)
{
	google::InitGoogleLogging(argv[0]);
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
