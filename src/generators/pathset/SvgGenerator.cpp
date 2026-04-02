#define NANOSVG_IMPLEMENTATION
#include "utils/nanosvg.h"
#include "generators/pathset/SvgGenerator.h"

bool SvgGenerator::loadSvg(const std::string &path)
{
	if (path.empty()) return false;

	// Parse SVG requesting millimetre units at 96 DPI (standard screen DPI).
	// nanosvg converts the SVG viewBox / width+height into mm for us.
	NSVGimage *image = nsvgParseFromFile(path.c_str(), "mm", 96.0f);
	if (!image)
	{
		LOG(WARNING) << "nanosvg failed to parse: " << path;
		m_svg = SvgPathData{};
		return false;
	}

	SvgPathData svg;
	svg.width  = image->width;
	svg.height = image->height;

	for (NSVGshape *shape = image->shapes; shape != nullptr; shape = shape->next)
	{
		// Skip invisible shapes
		if (!(shape->flags & NSVG_FLAGS_VISIBLE))
			continue;

		for (NSVGpath *npath = shape->paths; npath != nullptr; npath = npath->next)
		{
			if (npath->npts < 2) continue;

			SvgPathData::SubPath sp;
			sp.closed = npath->closed != 0;

			// nanosvg stores points as: x0,y0, [cp1x,cp1y, cp2x,cp2y, x,y] ...
			// First point is the move-to, then groups of 3 points per cubic.
			int numCubics = (npath->npts - 1) / 3;
			sp.beziers.reserve(numCubics);

			for (int i = 0; i < numCubics; ++i)
			{
				float *p = &npath->pts[i * 3 * 2];
				SvgPathData::CubicBezier cb;
				cb.x0 = p[0]; cb.y0 = p[1];
				cb.x1 = p[2]; cb.y1 = p[3];
				cb.x2 = p[4]; cb.y2 = p[5];
				cb.x3 = p[6]; cb.y3 = p[7];
				sp.beziers.push_back(cb);
			}

			if (!sp.beziers.empty())
				svg.subPaths.push_back(std::move(sp));
		}
	}

	nsvgDelete(image);

	m_svg = std::move(svg);
	m_version.fetch_add(1);

	LOG(INFO) << "Loaded SVG: " << path
	          << " (" << m_svg.subPaths.size() << " sub-paths, "
	          << svg.width << "x" << svg.height << " mm)";
	return true;
}
