#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Standard on-disk location for project .json files, plus helpers for
// listing and managing the projects stored there.
namespace projects
{

struct ProjectInfo
{
	std::string name;           // file stem, e.g. "spirals"
	std::string path;           // full path to the .json file
	uint64_t sizeBytes{0};
	int64_t modifiedUnix{0};    // seconds since epoch, for sorting
	std::string modifiedString; // "YYYY-MM-DD HH:MM" in local time
};

// Directory where project files live (~/Documents/Minotaur/Projects,
// falling back to ./projects if the home directory is unavailable).
// Created on first call if missing.
std::string projectsDirectory();

// <projectsDirectory>/default.json - used when no path is given on the CLI.
std::string defaultProjectPath();

// Sanitized "<projectsDirectory>/<name>.json" for a user-entered name.
std::string pathForName(const std::string &name);

// All *.json files in the projects directory, newest first.
std::vector<ProjectInfo> listProjects();

// Remove a project file. Returns true on success.
bool deleteProject(const std::string &path);

}
