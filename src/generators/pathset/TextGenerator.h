#pragma once
#include <string>
#include "generators/GeneratorTyped.h"
#include "core/Core.h"
#include "utils/VectorFont.h"

// Generates stroke-font text paths.
// The text content is a string member (not a float parameter); height and
// letter-spacing are float parameters as usual.
struct TextGenerator : public GeneratorTyped<TextGenerator, PathSet>
{
	// The text to render.  Changing this via setText() bumps the version.
	std::string text{"Hello"};

	TextGenerator()
	{
		m_parameters["height_mm"]      = FilterParameter{"Height (mm)",         1.0f, 100.0f, 12.0f};
		m_parameters["letter_spacing"] = FilterParameter{"Letter Spacing (u)",  0.0f,  10.0f,  2.0f};
	}

	const char *name() const override { return "Text"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void setText(const std::string &t)
	{
		text = t;
		m_version.fetch_add(1);
	}

	std::unique_ptr<GeneratorBase> clone() const override
	{
		auto copy = std::make_unique<TextGenerator>();
		for (const auto &kv : m_parameters)
			copy->setParameter(kv.first, kv.second.value);
		copy->setText(text);
		return copy;
	}

	void generateTyped(PathSet &out) const override
	{
		float height  = m_parameters.at("height_mm").value;
		float spacing = m_parameters.at("letter_spacing").value;

		out.paths.clear();
		auto paths = VectorFont::textToPaths(text, 0.0f, 0.0f, height, spacing);
		out.paths = std::move(paths);
	}
};
