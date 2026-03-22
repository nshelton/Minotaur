#include "Clipboard.h"

#ifdef __APPLE__

#import <AppKit/AppKit.h>

namespace Clipboard
{

std::vector<uint8_t> getImageData()
{
	NSPasteboard *pb = [NSPasteboard generalPasteboard];

	// Try PNG first
	NSData *data = [pb dataForType:NSPasteboardTypePNG];
	if (data)
	{
		const uint8_t *bytes = static_cast<const uint8_t *>(data.bytes);
		return std::vector<uint8_t>(bytes, bytes + data.length);
	}

	// Try TIFF (screenshots on macOS are often TIFF) and convert to PNG
	data = [pb dataForType:NSPasteboardTypeTIFF];
	if (data)
	{
		NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:data];
		if (rep)
		{
			NSData *pngData = [rep representationUsingType:NSBitmapImageFileTypePNG
			                                   properties:@{}];
			if (pngData)
			{
				const uint8_t *bytes = static_cast<const uint8_t *>(pngData.bytes);
				return std::vector<uint8_t>(bytes, bytes + pngData.length);
			}
		}
	}

	return {};
}

} // namespace Clipboard

#endif // __APPLE__
