#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>
#include <string>

#include "generators/pathset/TimestampGenerator.h"

namespace
{
// 2026-01-05T12:34:56Z
constexpr long long kEpoch = 1767616496LL;

// GeneratorBase holds atomics, so the type is neither copyable nor movable;
// hand back a pointer rather than a value.
std::unique_ptr<TimestampGenerator> makeAt(long long epoch, int format, bool utc)
{
	auto g = std::make_unique<TimestampGenerator>();
	g->setStringParameter("unix_time", std::to_string(epoch));
	g->setParameter("format", static_cast<float>(format));
	g->setParameter("utc", utc ? 1.0f : 0.0f);
	return g;
}
}

TEST(TimestampGenerator, DefaultsToCurrentUnixTime)
{
	const auto before = static_cast<long long>(std::time(nullptr));
	TimestampGenerator g;
	const auto after = static_cast<long long>(std::time(nullptr));

	// Default format is Unix, so the rendered text is the captured epoch.
	const long long shown = std::stoll(g.formatted());
	EXPECT_GE(shown, before);
	EXPECT_LE(shown, after);
}

TEST(TimestampGenerator, UnixFormatEchoesStoredValue)
{
	auto g = makeAt(kEpoch, TimestampGenerator::Unix, true);
	EXPECT_EQ(g->formatted(), "1767616496");
}

TEST(TimestampGenerator, UnixFormatIgnoresUtcToggle)
{
	// Unix seconds are zone-independent by definition.
	EXPECT_EQ(makeAt(kEpoch, TimestampGenerator::Unix, true)->formatted(),
	          makeAt(kEpoch, TimestampGenerator::Unix, false)->formatted());
}

TEST(TimestampGenerator, DayMonthYearFormatUtc)
{
	auto g = makeAt(kEpoch, TimestampGenerator::DayMonthYear, true);
	EXPECT_EQ(g->formatted(), "5 Jan 2026");
}

TEST(TimestampGenerator, DayMonthYearHasNoLeadingZeroOnDay)
{
	// 2026-01-05T00:00:00Z — the day should read "5", not "05".
	auto g = makeAt(1767571200LL, TimestampGenerator::DayMonthYear, true);
	EXPECT_EQ(g->formatted(), "5 Jan 2026");
}

TEST(TimestampGenerator, IsoFormatUtcIsZoneTagged)
{
	auto g = makeAt(kEpoch, TimestampGenerator::Iso8601, true);
	EXPECT_EQ(g->formatted(), "2026-01-05T12:34:56Z");
}

TEST(TimestampGenerator, IsoFormatLocalIsNotZoneTagged)
{
	// The local rendering depends on the host zone, so only assert the shape:
	// same width minus the Z, and no false UTC claim.
	auto g = makeAt(kEpoch, TimestampGenerator::Iso8601, false);
	const std::string s = g->formatted();
	EXPECT_EQ(s.size(), 19u);
	EXPECT_NE(s.back(), 'Z');
	EXPECT_EQ(s[4], '-');
	EXPECT_EQ(s[10], 'T');
	EXPECT_EQ(s[13], ':');
}

TEST(TimestampGenerator, TypingNowRestampsToCurrentTime)
{
	auto g = makeAt(kEpoch, TimestampGenerator::Unix, true);
	ASSERT_EQ(g->formatted(), "1767616496");

	const auto before = static_cast<long long>(std::time(nullptr));
	g->setStringParameter("unix_time", "now");
	const auto after = static_cast<long long>(std::time(nullptr));

	const long long shown = std::stoll(g->formatted());
	EXPECT_GE(shown, before);
	EXPECT_LE(shown, after);
	// The magic word must not survive in the stored parameter.
	EXPECT_NE(g->stringParameter("unix_time"), "now");
}

TEST(TimestampGenerator, ParameterChangeBumpsVersion)
{
	TimestampGenerator g;
	const uint64_t v0 = g.paramVersion();
	g.setParameter("format", static_cast<float>(TimestampGenerator::Iso8601));
	EXPECT_GT(g.paramVersion(), v0);

	const uint64_t v1 = g.paramVersion();
	g.setStringParameter("unix_time", "1000000000");
	EXPECT_GT(g.paramVersion(), v1);
}

TEST(TimestampGenerator, CloneKeepsCapturedInstant)
{
	auto g = makeAt(kEpoch, TimestampGenerator::Iso8601, true);
	auto copy = g->clone();
	auto *tc = dynamic_cast<TimestampGenerator *>(copy.get());
	ASSERT_NE(tc, nullptr);
	// A fresh TimestampGenerator captures "now" in its constructor; cloning
	// must overwrite that with the source instant, not keep the new one.
	EXPECT_EQ(tc->formatted(), "2026-01-05T12:34:56Z");
}

TEST(TimestampGenerator, GeneratesPathsThatScaleWithHeight)
{
	auto g = makeAt(kEpoch, TimestampGenerator::DayMonthYear, true);

	PathSet small;
	g->generateTyped(small);
	ASSERT_FALSE(small.paths.empty());

	g->setParameter("height_mm", 24.0f);
	PathSet large;
	g->generateTyped(large);

	// Same glyphs, so the same stroke topology, just bigger.
	ASSERT_EQ(small.paths.size(), large.paths.size());

	auto maxAbsY = [](const PathSet &ps) {
		float m = 0.0f;
		for (const auto &p : ps.paths)
			for (const auto &pt : p.points)
				m = std::max(m, std::abs(pt.y));
		return m;
	};
	EXPECT_GT(maxAbsY(large), maxAbsY(small) * 1.5f);
}

TEST(TimestampGenerator, RegeneratingIsStable)
{
	// The instant is frozen, so output must not drift between evaluations.
	auto g = makeAt(kEpoch, TimestampGenerator::Iso8601, true);
	const std::string a = g->formatted();
	const std::string b = g->formatted();
	EXPECT_EQ(a, b);
}

TEST(TimestampGenerator, OutputKindIsPathSet)
{
	TimestampGenerator g;
	EXPECT_EQ(g.outputKind(), LayerKind::PathSet);
	EXPECT_FALSE(g.isAsync());
	EXPECT_STREQ(g.name(), "Timestamp");
}
