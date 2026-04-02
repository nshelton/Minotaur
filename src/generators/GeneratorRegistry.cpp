#include "GeneratorRegistry.h"

#include "generators/pathset/CircleGenerator.h"
#include "generators/pathset/SquareGenerator.h"
#include "generators/pathset/StarGenerator.h"
#include "generators/pathset/TextGenerator.h"
#include "generators/pathset/GridGenerator.h"
#include "generators/bitmap/GradientGenerator.h"
#include "generators/bitmap/CheckerboardGenerator.h"
#include "generators/bitmap/RadialGenerator.h"
#include "generators/mesh/MeshGenerator.h"
#include "generators/pathset/OsmGenerator.h"
#include "generators/pathset/SvgGenerator.h"

GeneratorRegistry &GeneratorRegistry::instance()
{
	static GeneratorRegistry s;
	return s;
}

void GeneratorRegistry::registerGenerator(const GeneratorInfo &info)
{
	m_generators.push_back(info);
}

const std::vector<GeneratorInfo> &GeneratorRegistry::all() const
{
	return m_generators;
}

void GeneratorRegistry::initDefaults()
{
	auto &reg = instance();

	reg.registerGenerator(GeneratorInfo{
		"Circle",
		LayerKind::PathSet,
		[]() { return std::make_unique<CircleGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Square",
		LayerKind::PathSet,
		[]() { return std::make_unique<SquareGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Star",
		LayerKind::PathSet,
		[]() { return std::make_unique<StarGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Text",
		LayerKind::PathSet,
		[]() { return std::make_unique<TextGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Grid",
		LayerKind::PathSet,
		[]() { return std::make_unique<GridGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Gradient",
		LayerKind::Bitmap,
		[]() { return std::make_unique<GradientGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Checkerboard",
		LayerKind::Bitmap,
		[]() { return std::make_unique<CheckerboardGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Radial",
		LayerKind::Bitmap,
		[]() { return std::make_unique<RadialGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"Mesh",
		LayerKind::PathSet,
		[]() { return std::make_unique<MeshGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"OSM Tiles",
		LayerKind::PathSet,
		[]() { return std::make_unique<OsmGenerator>(); }
	});

	reg.registerGenerator(GeneratorInfo{
		"SVG",
		LayerKind::PathSet,
		[]() { return std::make_unique<SvgGenerator>(); }
	});
}
