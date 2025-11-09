#pragma once

#include <atomic>

#include "../Filter.h"

// Skeletonize black pixels and trace the 1px-wide skeleton into polylines
struct SkeletonizeFilter : public FilterTyped<Bitmap, Bitmap>
{
	SkeletonizeFilter()
	{
	}

	const char *name() const override { return "Skeletonize"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const Bitmap &in, Bitmap &out) const override;
};


