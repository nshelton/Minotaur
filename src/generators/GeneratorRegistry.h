#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "filters/Types.h"
#include "generators/GeneratorBase.h"

struct GeneratorInfo
{
	std::string name;
	LayerKind outputKind;
	std::function<std::unique_ptr<GeneratorBase>()> factory;
};

class GeneratorRegistry
{
public:
	static GeneratorRegistry &instance();

	void registerGenerator(const GeneratorInfo &info);

	const std::vector<GeneratorInfo> &all() const;

	// One-time initialization of built-in generators
	static void initDefaults();

private:
	std::vector<GeneratorInfo> m_generators;
};
