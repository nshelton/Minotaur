#include "utils/ProjectStore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include <glog/logging.h>

namespace fs = std::filesystem;

namespace projects
{

static fs::path homeDirectory()
{
	if (const char *home = std::getenv("HOME"))
		return fs::path(home);
	if (const char *profile = std::getenv("USERPROFILE"))
		return fs::path(profile);
	return fs::current_path();
}

std::string projectsDirectory()
{
	fs::path dir = homeDirectory() / "Documents" / "Minotaur" / "Projects";
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec)
	{
		LOG(WARNING) << "Failed to create projects directory " << dir.string()
		             << " (" << ec.message() << "); falling back to ./projects";
		dir = fs::path("projects");
		fs::create_directories(dir, ec);
	}
	return dir.string();
}

std::string defaultProjectPath()
{
	return (fs::path(projectsDirectory()) / "default.json").string();
}

std::string pathForName(const std::string &name)
{
	std::string clean;
	clean.reserve(name.size());
	for (char c : name)
	{
		const bool ok = std::isalnum(static_cast<unsigned char>(c)) ||
		                c == '-' || c == '_' || c == ' ' || c == '.';
		clean.push_back(ok ? c : '_');
	}

	// Trim leading/trailing spaces and dots (hidden files, "name. " typos)
	const auto first = clean.find_first_not_of(" .");
	const auto last = clean.find_last_not_of(" .");
	clean = (first == std::string::npos) ? std::string{} : clean.substr(first, last - first + 1);

	// Strip a typed ".json" so "foo.json" doesn't become "foo.json.json"
	const std::string ext = ".json";
	if (clean.size() > ext.size() && clean.compare(clean.size() - ext.size(), ext.size(), ext) == 0)
		clean.erase(clean.size() - ext.size());

	if (clean.empty())
		clean = "untitled";

	return (fs::path(projectsDirectory()) / (clean + ext)).string();
}

static int64_t toUnixSeconds(fs::file_time_type tp)
{
	using namespace std::chrono;
	// C++17 has no clock_cast; convert via the offset between the two clocks' nows
	const auto sys = time_point_cast<system_clock::duration>(
		tp - fs::file_time_type::clock::now() + system_clock::now());
	return duration_cast<seconds>(sys.time_since_epoch()).count();
}

static std::string formatLocalTime(int64_t unixSeconds)
{
	std::time_t t = static_cast<std::time_t>(unixSeconds);
	std::tm tmv{};
#ifdef _WIN32
	localtime_s(&tmv, &t);
#else
	localtime_r(&t, &tmv);
#endif
	char buf[32];
	if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv) == 0)
		return {};
	return buf;
}

std::vector<ProjectInfo> listProjects()
{
	std::vector<ProjectInfo> out;
	std::error_code ec;
	fs::directory_iterator it(projectsDirectory(), ec);
	if (ec)
	{
		LOG(WARNING) << "Failed to list projects directory: " << ec.message();
		return out;
	}

	for (const auto &entry : it)
	{
		if (!entry.is_regular_file(ec) || ec)
			continue;
		if (entry.path().extension() != ".json")
			continue;

		ProjectInfo info;
		info.name = entry.path().stem().string();
		info.path = entry.path().string();
		info.sizeBytes = static_cast<uint64_t>(entry.file_size(ec));
		if (ec)
			info.sizeBytes = 0;
		const auto mtime = entry.last_write_time(ec);
		if (!ec)
		{
			info.modifiedUnix = toUnixSeconds(mtime);
			info.modifiedString = formatLocalTime(info.modifiedUnix);
		}
		out.push_back(std::move(info));
	}

	std::sort(out.begin(), out.end(), [](const ProjectInfo &a, const ProjectInfo &b) {
		return a.modifiedUnix > b.modifiedUnix;
	});
	return out;
}

bool deleteProject(const std::string &path)
{
	// Only ever delete .json files inside the projects directory
	std::error_code ec;
	const fs::path target = fs::weakly_canonical(fs::path(path), ec);
	const fs::path dir = fs::weakly_canonical(fs::path(projectsDirectory()), ec);
	const std::string targetStr = target.string();
	const std::string dirStr = dir.string();
	if (target.extension() != ".json" ||
	    targetStr.compare(0, dirStr.size(), dirStr) != 0)
	{
		LOG(ERROR) << "Refusing to delete non-project file: " << path;
		return false;
	}

	const bool ok = fs::remove(target, ec) && !ec;
	if (!ok)
		LOG(ERROR) << "Failed to delete project " << path << ": " << ec.message();
	else
		LOG(INFO) << "Deleted project: " << path;
	return ok;
}

}
