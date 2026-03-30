#pragma once

#include <cmath>
#include <cstdint>
#include "core/Bitmap.h"
#include "core/Pathset.h"

inline Bitmap makeSolidBitmap(int w, int h, uint8_t value, float pixel_size_mm = 1.0f)
{
	Bitmap bmp;
	bmp.width_px = static_cast<size_t>(w);
	bmp.height_px = static_cast<size_t>(h);
	bmp.pixel_size_mm = pixel_size_mm;
	bmp.pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), value);
	return bmp;
}

inline Bitmap makeCheckerboard(int w, int h, int blockSize, float pixel_size_mm = 1.0f)
{
	Bitmap bmp;
	bmp.width_px = static_cast<size_t>(w);
	bmp.height_px = static_cast<size_t>(h);
	bmp.pixel_size_mm = pixel_size_mm;
	bmp.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			bool white = ((x / blockSize) + (y / blockSize)) % 2 == 0;
			bmp.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = white ? 255 : 0;
		}
	}
	return bmp;
}

inline PathSet makeSquarePath(float side)
{
	PathSet ps;
	Path p;
	p.closed = true;
	p.points = {
		Vec2(0.0f, 0.0f),
		Vec2(side, 0.0f),
		Vec2(side, side),
		Vec2(0.0f, side)
	};
	ps.paths.push_back(std::move(p));
	return ps;
}

inline PathSet makeCirclePath(float radius, int segments)
{
	PathSet ps;
	Path p;
	p.closed = true;
	for (int i = 0; i < segments; ++i)
	{
		float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(segments);
		p.points.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
	}
	ps.paths.push_back(std::move(p));
	return ps;
}
