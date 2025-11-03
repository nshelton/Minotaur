#pragma once

#include <vector>
#include <unordered_map>
#include <string>

#include "core/Pathset.h"

// Stroke-based vector font to convert text into polylines (Path objects)
// Coordinates in glyph data use Y up negative; conversion flips Y to match page coords.
class VectorFont
{
public:
	struct Command
	{
		char op;
		float x;
		float y;
	};
	using Glyph = std::vector<Command>;
	// Convert text to a list of open paths positioned at origin_mm with specified height (mm)
	// letterSpacingUnits adds extra advance in font units between glyphs
	static std::vector<Path> textToPaths(const std::string &text,
										 float originX_mm,
										 float originY_mm,
										 float height_mm = 10.0f,
										 float letterSpacingUnits = 2.0f);

private:
	static const std::unordered_map<char, Glyph> &glyphs();
	static std::vector<Path> glyphToPaths(const Glyph &cmds, float offsetX_mm, float offsetY_mm, float scale);
	static float measureGlyphWidthUnits(const Glyph & /*cmds*/);
};
