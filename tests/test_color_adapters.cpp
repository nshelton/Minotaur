#include <gtest/gtest.h>

#include "filters/bitmap/ColorAdapters.h"
#include "filters/bitmap/BlurFilter.h"
#include "filters/bitmap/LevelsFilter.h"

namespace
{
	// Deterministic per-channel test pattern
	ColorImage makePattern(size_t w, size_t h, float pixelSizeMm = 0.25f)
	{
		ColorImage img;
		img.width_px = w;
		img.height_px = h;
		img.pixel_size_mm = pixelSizeMm;
		img.pixels.resize(w * h * 3);
		for (size_t y = 0; y < h; ++y)
		{
			for (size_t x = 0; x < w; ++x)
			{
				const size_t i = (y * w + x) * 3;
				img.pixels[i + 0] = static_cast<uint8_t>((x * 37 + y * 11) % 256);
				img.pixels[i + 1] = static_cast<uint8_t>((x * 5 + y * 91) % 256);
				img.pixels[i + 2] = static_cast<uint8_t>((x * 149 + y * 3) % 256);
			}
		}
		return img;
	}

	Bitmap channelOf(const ColorImage &img, int c)
	{
		Bitmap b;
		b.width_px = img.width_px;
		b.height_px = img.height_px;
		b.pixel_size_mm = img.pixel_size_mm;
		b.pixels.resize(img.width_px * img.height_px);
		for (size_t i = 0; i < b.pixels.size(); ++i)
			b.pixels[i] = img.pixels[i * 3 + static_cast<size_t>(c)];
		return b;
	}
}

// The registry resolves saved filters by name; pin the display names.
TEST(ColorAdapters, DisplayNames)
{
	EXPECT_EQ(perChannelColorName<BlurFilter>(), "Blur (RGB)");
	EXPECT_EQ(perChannelColorName<LevelsFilter>(), "Levels (RGB)");

	PerChannelColorFilter<BlurFilter> blur;
	EXPECT_STREQ(blur.name(), "Blur (RGB)");
	EXPECT_EQ(blur.inputKind(), LayerKind::ColorImage);
	EXPECT_EQ(blur.outputKind(), LayerKind::ColorImage);
}

// Parameters are lifted from the wrapped grayscale filter
TEST(ColorAdapters, InheritsWrappedParameters)
{
	PerChannelColorFilter<LevelsFilter> levels;
	LevelsFilter gray;
	ASSERT_EQ(levels.m_parameters.size(), gray.m_parameters.size());
	for (const auto &kv : gray.m_parameters)
	{
		ASSERT_TRUE(levels.m_parameters.count(kv.first)) << kv.first;
		EXPECT_FLOAT_EQ(levels.m_parameters.at(kv.first).value, kv.second.value);
	}
}

// Each channel must match the grayscale filter run on that channel alone
TEST(ColorAdapters, BlurMatchesPerChannelGrayscale)
{
	const ColorImage in = makePattern(19, 13);

	PerChannelColorFilter<BlurFilter> color;
	color.setParameter("radius", 3.0f);

	ColorImage out;
	color.applyTyped(in, out);

	ASSERT_EQ(out.width_px, in.width_px);
	ASSERT_EQ(out.height_px, in.height_px);
	EXPECT_FLOAT_EQ(out.pixel_size_mm, in.pixel_size_mm);
	ASSERT_EQ(out.pixels.size(), in.pixels.size());

	BlurFilter gray;
	gray.setParameter("radius", 3.0f);
	for (int c = 0; c < 3; ++c)
	{
		Bitmap expected;
		gray.applyTyped(channelOf(in, c), expected);
		ASSERT_EQ(expected.pixels.size(), in.width_px * in.height_px);
		for (size_t i = 0; i < expected.pixels.size(); ++i)
		{
			ASSERT_EQ(out.pixels[i * 3 + static_cast<size_t>(c)], expected.pixels[i])
				<< "channel " << c << " pixel " << i;
		}
	}
}

// Radius 0 is a pass-through, and channels must not bleed into each other
TEST(ColorAdapters, BlurRadiusZeroIsIdentity)
{
	const ColorImage in = makePattern(8, 8);

	PerChannelColorFilter<BlurFilter> color;
	color.setParameter("radius", 0.0f);

	ColorImage out;
	color.applyTyped(in, out);
	EXPECT_EQ(out.pixels, in.pixels);
}

TEST(ColorAdapters, LevelsInvertsEachChannel)
{
	const ColorImage in = makePattern(6, 4);

	PerChannelColorFilter<LevelsFilter> color;
	color.setParameter("invert", 1.0f);

	ColorImage out;
	color.applyTyped(in, out);

	ASSERT_EQ(out.pixels.size(), in.pixels.size());
	for (size_t i = 0; i < in.pixels.size(); ++i)
		EXPECT_EQ(out.pixels[i], static_cast<uint8_t>(255 - in.pixels[i])) << "index " << i;
}

TEST(ColorAdapters, EmptyInputProducesEmptyOutput)
{
	ColorImage in;
	in.pixel_size_mm = 0.5f;

	PerChannelColorFilter<BlurFilter> color;
	ColorImage out;
	color.applyTyped(in, out);

	EXPECT_EQ(out.width_px, 0u);
	EXPECT_EQ(out.height_px, 0u);
	EXPECT_TRUE(out.pixels.empty());
}

// A malformed input (pixel buffer shorter than w*h*3) must not read out of bounds
TEST(ColorAdapters, TruncatedInputIsRejected)
{
	ColorImage in;
	in.width_px = 4;
	in.height_px = 4;
	in.pixels.resize(4 * 4 * 3 - 5, 128);

	PerChannelColorFilter<BlurFilter> color;
	ColorImage out;
	color.applyTyped(in, out);

	EXPECT_TRUE(out.pixels.empty());
}

// Changing a parameter must bump the version so FilterChain caches invalidate
TEST(ColorAdapters, ParamVersionAdvancesOnSet)
{
	PerChannelColorFilter<BlurFilter> color;
	const uint64_t before = color.paramVersion();
	color.setParameter("radius", 5.0f);
	EXPECT_GT(color.paramVersion(), before);
}
