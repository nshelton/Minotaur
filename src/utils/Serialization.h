#pragma once

#include <string>

struct PageModel;
class Camera;
class Renderer;

namespace serialization {

// Save PageModel to JSON file at path. Pretty prints.
// Returns true on success; false and fills errorOut on failure.
bool savePageModel(const PageModel& model, const std::string& filePath, std::string* errorOut = nullptr);

// Load PageModel from JSON file at path. Existing model contents will be replaced.
// Returns true on success; false and fills errorOut on failure.
bool loadPageModel(PageModel& model, const std::string& filePath, std::string* errorOut = nullptr);

// Save/Load full project (page + camera + renderer) to JSON file.
bool saveProject(const PageModel& model, const Camera& camera, const Renderer& renderer, const std::string& filePath, std::string* errorOut = nullptr);
bool loadProject(PageModel& model, Camera& camera, Renderer& renderer, const std::string& filePath, std::string* errorOut = nullptr);

}


