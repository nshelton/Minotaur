#pragma once

#include <string>
#include "plotters/PlotterConfig.h"

struct PageModel;
class Camera;

namespace serialization {

struct RenderSettings {
    float lineWidth{1.0f};
    float nodeDiameter{8.0f};
};

// Save PageModel to JSON file at path. Pretty prints.
// Returns true on success; false and fills errorOut on failure.
bool savePageModel(const PageModel& model, const std::string& filePath, std::string* errorOut = nullptr);

// Load PageModel from JSON file at path. Existing model contents will be replaced.
// Returns true on success; false and fills errorOut on failure.
bool loadPageModel(PageModel& model, const std::string& filePath, std::string* errorOut = nullptr);

// Save/Load full project (page + camera + render settings + plotter config) to JSON file.
bool saveProject(const PageModel& model, const Camera& camera, const RenderSettings& render,
                 const PlotterConfig& plotter,
                 const std::string& filePath, std::string* errorOut = nullptr);
bool loadProject(PageModel& model, Camera& camera, RenderSettings& render,
                 PlotterConfig& plotter,
                 const std::string& filePath, std::string* errorOut = nullptr);

}


