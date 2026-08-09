#pragma once
#include <cstdlib>
#include <ctime>
#include <string>

#include <fmt/format.h>

#include "generators/GeneratorTyped.h"
#include "generators/pathset/TextGenerator.h"
#include "core/Core.h"

// Generates a timestamp as stroke-font paths. This is a thin wrapper around
// TextGenerator: it formats an instant into a string and hands that to a
// TextGenerator for the actual glyph work, so both share one font path.
//
// The instant is captured once, at construction, and stored in the "unix_time"
// string parameter. Keeping it as a string (rather than a float parameter)
// matters twice over: it survives save/load exactly, where a float32 would
// quantise present-day epoch seconds to ~128s buckets, and it means a reopened
// project replots the date it was made rather than the date it was reopened.
//
// To re-stamp with the current clock, type "now" into the unix_time field —
// it is replaced with the epoch seconds at that moment.
struct TimestampGenerator : public GeneratorTyped<TimestampGenerator, PathSet>
{
	// Values of the "format" enum parameter.
	enum Format : int { Unix = 0, DayMonthYear = 1, Iso8601 = 2 };

	TimestampGenerator()
	{
		m_stringParameters["unix_time"] = fmt::format("{}", static_cast<long long>(std::time(nullptr)));

		m_parameters["format"] = FilterParameter{
			"Format", 0.0f, 2.0f, static_cast<float>(Unix),
			FilterParameter::Enum, {"Unix", "5 Jan 2026", "ISO 8601"}
		};
		m_parameters["utc"]            = FilterParameter{"UTC",                0.0f,   1.0f,  0.0f, FilterParameter::Bool};
		m_parameters["height_mm"]      = FilterParameter{"Height (mm)",        1.0f, 100.0f, 12.0f};
		m_parameters["letter_spacing"] = FilterParameter{"Letter Spacing (u)", 0.0f,  10.0f,  2.0f};
	}

	const char *name() const override { return "Timestamp"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	// Typing "now" into the field snaps it to the current clock. This is the
	// only way to re-stamp, since generateTyped() is const and cannot capture.
	void onStringParameterChanged(const std::string &key) override
	{
		if (key != "unix_time")
			return;
		const std::string &v = m_stringParameters["unix_time"];
		if (v == "now" || v == "NOW" || v == "Now")
		{
			// Assign directly rather than via setStringParameter: that would
			// re-enter this hook, and the version bump has already happened.
			m_stringParameters["unix_time"] = fmt::format("{}", static_cast<long long>(std::time(nullptr)));
		}
	}

	void generateTyped(PathSet &out) const override
	{
		// Delegate to a TextGenerator so glyph layout lives in exactly one place.
		TextGenerator text;
		text.setStringParameter("text", formatted());
		text.setParameter("height_mm", m_parameters.at("height_mm").value);
		text.setParameter("letter_spacing", m_parameters.at("letter_spacing").value);
		text.generateTyped(out);
	}

	// The timestamp as it will be drawn.
	std::string formatted() const
	{
		const std::time_t t = static_cast<std::time_t>(
			std::strtoll(stringParameter("unix_time").c_str(), nullptr, 10));

		const int fmtIdx = static_cast<int>(m_parameters.at("format").value + 0.5f);
		if (fmtIdx == Unix)
			return fmt::format("{}", static_cast<long long>(t));

		const bool utc = m_parameters.at("utc").value > 0.5f;
		const std::tm tm = breakDown(t, utc);

		if (fmtIdx == DayMonthYear)
		{
			static const char *kMonths[12] = {
				"Jan", "Feb", "Mar", "Apr", "May", "Jun",
				"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
			};
			const int mon = (tm.tm_mon >= 0 && tm.tm_mon < 12) ? tm.tm_mon : 0;
			return fmt::format("{} {} {}", tm.tm_mday, kMonths[mon], 1900 + tm.tm_year);
		}

		// ISO 8601. Only tag the zone when it is actually UTC; a local time
		// carries no offset here, so claiming one would be a lie.
		return fmt::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}{}",
			1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, utc ? "Z" : "");
	}

private:
	// Thread-safe calendar breakdown; std::gmtime/std::localtime share a buffer.
	static std::tm breakDown(std::time_t t, bool utc)
	{
		std::tm out{};
#ifdef _WIN32
		if (utc)
			gmtime_s(&out, &t);
		else
			localtime_s(&out, &t);
#else
		if (utc)
			gmtime_r(&t, &out);
		else
			localtime_r(&t, &out);
#endif
		return out;
	}
};
