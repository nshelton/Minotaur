#pragma once

#include <cstdint>
#include <vector>

namespace Clipboard
{
	// Returns clipboard image data as PNG bytes, or empty vector if no image on clipboard.
	std::vector<uint8_t> getImageData();
}
