#include "utils/Serialization.h"

#include <fstream>
#include <sstream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "Page.h"
#include "core/Core.h"

using nlohmann::json;

// JSON conversions for core types

static void to_json(json &j, const Vec2 &v)
{
    j = json{{"x", v.x}, {"y", v.y}};
}

static void from_json(const json &j, Vec2 &v)
{
    v.x = j.value("x", 0.0f);
    v.y = j.value("y", 0.0f);
}

static void to_json(json &j, const Color &c)
{
    j = json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}

static void from_json(const json &j, Color &c)
{
    c.r = j.value("r", 1.0f);
    c.g = j.value("g", 1.0f);
    c.b = j.value("b", 1.0f);
    c.a = j.value("a", 1.0f);
}

static void to_json(json &j, const Mat3 &m)
{
    j = json::array();
    for (int i = 0; i < 9; ++i)
    {
        j.push_back(m.m[i]);
    }
}

static void from_json(const json &j, Mat3 &m)
{
    // Initialize identity if missing or invalid
    Mat3 identity;
    for (int i = 0; i < 9; ++i)
        m.m[i] = identity.m[i];
    if (!j.is_array() || j.size() != 9)
        return;
    for (int i = 0; i < 9; ++i)
    {
        m.m[i] = j.at(static_cast<size_t>(i)).get<float>();
    }
}

static void to_json(json &j, const Path &p)
{
    j = json{
        {"closed", p.closed},
        {"points", p.points}};
}

static void from_json(const json &j, Path &p)
{
    p.closed = j.value("closed", false);
    p.points = j.value("points", std::vector<Vec2>{});
}

static void to_json(json &j, const PathSet &ps)
{
    j = json{
        {"color", ps.color},
        {"paths", ps.paths}};
}

static void from_json(const json &j, PathSet &ps)
{
    ps.color = j.value("color", Color{});
    ps.paths = j.value("paths", std::vector<Path>{});
    // AABB can be recomputed lazily
}

static void to_json(json &j, const Entity &e)
{
    j = json{
        {"id", e.id},
        {"name", e.name},
        {"pathset", e.pathset},
        {"localToPage", e.localToPage}};
}

static void from_json(const json &j, Entity &e)
{
    e.id = j.value("id", 0);
    e.name = j.value("name", std::string{});
    e.pathset = j.value("pathset", PathSet{});
    if (j.contains("localToPage"))
    {
        e.localToPage = j.at("localToPage").get<Mat3>();
    }
    else
    {
        e.localToPage = Mat3();
    }
}

namespace serialization
{

    static constexpr int kSchemaVersion = 1;

    bool savePageModel(const PageModel &model, const std::string &filePath, std::string *errorOut)
    {
        try
        {
            // Flatten map<int, Entity> to array sorted by key
            std::vector<Entity> entities;
            entities.reserve(model.entities.size());
            for (const auto &kv : model.entities)
            {
                entities.push_back(kv.second);
            }

            json j = {
                {"version", kSchemaVersion},
                {"page_size_mm", json{{"w", model.page_width_mm}, {"h", model.page_height_mm}}},
                {"mouse_pixel", model.mouse_pixel},
                {"mouse_page_mm", model.mouse_page_mm},
                {"entities", entities}};

            std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                if (errorOut)
                    *errorOut = "Failed to open file for writing: " + filePath;
                return false;
            }
            ofs << j.dump(2);
            return true;
        }
        catch (const std::exception &ex)
        {
            if (errorOut)
                *errorOut = ex.what();
            return false;
        }
    }

    bool loadPageModel(PageModel &model, const std::string &filePath, std::string *errorOut)
    {
        try
        {
            if (!std::filesystem::exists(filePath))
            {
                if (errorOut)
                    *errorOut = "File does not exist: " + filePath;
                return false;
            }
            std::ifstream ifs(filePath, std::ios::binary);
            if (!ifs)
            {
                if (errorOut)
                    *errorOut = "Failed to open file for reading: " + filePath;
                return false;
            }
            json j;
            ifs >> j;

            // optional version check
            int version = j.value("version", 1);
            (void)version; // keep reserved for future migrations

            model.mouse_pixel = j.value("mouse_pixel", Vec2{});
            model.mouse_page_mm = j.value("mouse_page_mm", Vec2{});

            model.entities.clear();
            if (j.contains("entities") && j["entities"].is_array())
            {
                for (const auto &je : j["entities"])
                {
                    Entity e = je.get<Entity>();
                    model.entities[e.id] = e;
                }
            }
            return true;
        }
        catch (const std::exception &ex)
        {
            if (errorOut)
                *errorOut = ex.what();
            return false;
        }
    }

} // namespace serialization
