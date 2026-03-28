#pragma once
#include <string>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"
#include "utils/VectorFont.h"

// Generates stroke-font text paths.
// The text content is stored as a string parameter ("text") so it is
// handled uniformly by the UI and serialization alongside all other
// string parameters on GeneratorBase.
struct TextGenerator : public GeneratorTyped<TextGenerator, PathSet>
{
	TextGenerator()
	{
		m_stringParameters["text"] = "Hello";
		m_parameters["height_mm"]      = FilterParameter{"Height (mm)",         1.0f, 100.0f, 12.0f};
		m_parameters["letter_spacing"] = FilterParameter{"Letter Spacing (u)",  0.0f,  10.0f,  2.0f};
	}

	const char *name() const override { return "Text"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void generateTyped(PathSet &out) const override
	{
		float height  = m_parameters.at("height_mm").value;
		float spacing = m_parameters.at("letter_spacing").value;
		const std::string &text = stringParameter("text");

		out.paths.clear();
		auto paths = VectorFont::textToPaths(text, 0.0f, 0.0f, height, spacing);
		out.paths = std::move(paths);
	}
};
