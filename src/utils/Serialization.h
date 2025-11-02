#pragma once

#include <string>

struct PageModel;

namespace serialization {

// Save PageModel to JSON file at path. Pretty prints.
// Returns true on success; false and fills errorOut on failure.
bool savePageModel(const PageModel& model, const std::string& filePath, std::string* errorOut = nullptr);

// Load PageModel from JSON file at path. Existing model contents will be replaced.
// Returns true on success; false and fills errorOut on failure.
bool loadPageModel(PageModel& model, const std::string& filePath, std::string* errorOut = nullptr);

}


