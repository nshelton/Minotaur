#include "utils/Serialization.h"

#include <fstream>
#include <sstream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "Page.h"
#include "core/Core.h"
#include "filters/FilterChain.h"
#include "filters/bitmap/BlurFilter.h"
#include "filters/bitmap/ThresholdFilter.h"
#include "filters/bitmap/TraceFilter.h"
#include "filters/pathset/SimplifyFilter.h"

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

// Bitmap JSON
static void to_json(json &j, const Bitmap &b)
{
    j = json{
        {"w_px", b.width_px},
        {"h_px", b.height_px},
        {"pixel_size_mm", b.pixel_size_mm},
        {"pixels", b.pixels}}; // store bytes as 0..255 ints
}

static void from_json(const json &j, Bitmap &b)
{
    b.width_px = j.value("w_px", 0);
    b.height_px = j.value("h_px", 0);
    b.pixel_size_mm = j.value("pixel_size_mm", 1.0f);
    b.pixels = j.value("pixels", std::vector<uint8_t>{});
}

// Tagged Entity JSON
static void to_json(json &j, const Entity &e)
{
    j["id"] = e.id;
    j["name"] = e.name;
    j["localToPage"] = e.localToPage;

    if (e.pathset())
    {
        j["type"] = "pathset";
        j["pathset"] = *e.pathset();
    }
    else
    {
        j["type"] = "bitmap";
        j["bitmap"] = *e.bitmap();
    }

    // Serialize filter chain (optional)
    json filters = json::array();
    size_t n = e.filterChain.filterCount();
    for (size_t i = 0; i < n; ++i)
    {
        const FilterBase *fb = e.filterChain.filterAt(i);
        if (!fb) continue;
        json jf;
        // Use the human-readable filter name as type tag
        jf["type"] = fb->name();
        jf["enabled"] = e.filterChain.isFilterEnabled(i);

        json params = json::object();
        // Generic parameters from FilterBase map (e.g., Threshold)
        for (const auto &kv : fb->m_parameters)
        {
            params[kv.first] = kv.second.value;
        }
        // Type-specific parameters (for filters not using m_parameters)
        if (auto blur = dynamic_cast<const BlurFilter *>(fb))
        {
            params["radiusPx"] = blur->radiusPx;
        }
        if (auto simp = dynamic_cast<const SimplifyFilter *>(fb))
        {
            params["toleranceMm"] = simp->toleranceMm;
        }
        if (auto trace = dynamic_cast<const TraceFilter *>(fb))
        {
            params["threshold"] = static_cast<int>(trace->threshold);
        }
        jf["params"] = std::move(params);
        filters.push_back(std::move(jf));
    }
    j["filters"] = std::move(filters);
}

static void from_json(const json &j, Entity &e)
{
    e.id = j.value("id", 0);
    e.name = j.value("name", std::string{});
    if (j.contains("localToPage"))
        e.localToPage = j.at("localToPage").get<Mat3>();
    else
        e.localToPage = Mat3();

    // Backward compatible: default to pathset
    std::string type = j.value("type", std::string("pathset"));
    if (type == "bitmap" && j.contains("bitmap"))
    {
        e.payload = j.at("bitmap").get<Bitmap>();
    }
    else
    {
        if (j.contains("pathset"))
            e.payload = j.at("pathset").get<PathSet>();
        else
            e.payload = PathSet{};
    }
}

namespace serialization
{

    static constexpr int kSchemaVersion = 1;

    bool savePageModel(const PageModel &model, const std::string &filePath, std::string *errorOut)
    {
        try
        {
            // Serialize entities directly (avoid copying Entities; FilterChain does not copy filters)
            json entities = json::array();
            entities.get_ptr<json::array_t*>()->reserve(model.entities.size());
            for (const auto &kv : model.entities)
            {
                json je;
                to_json(je, kv.second);
                entities.push_back(std::move(je));
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
                    // Ensure filter chain base is set after payload deserialization
                    e.refreshFilterBase();

            // Rebuild filter chain if present
            if (je.contains("filters") && je["filters"].is_array())
            {
                for (const auto &jf : je["filters"])
                {
                    std::unique_ptr<FilterBase> f;
                    std::string type = jf.value("type", std::string{});
                    bool enabled = jf.value("enabled", true);
                    const json &params = jf.contains("params") ? jf["params"] : json::object();

                    if (type == "Blur")
                    {
                        auto ptr = std::make_unique<BlurFilter>();
                        if (params.contains("radiusPx"))
                        {
                            ptr->setRadius(params.value("radiusPx", ptr->radiusPx));
                        }
                        f = std::move(ptr);
                    }
                    else if (type == "Threshold")
                    {
                        auto ptr = std::make_unique<ThresholdFilter>();
                        if (params.contains("threshold"))
                        {
                            ptr->setParameter("threshold", params.value("threshold", ptr->m_parameters["threshold"].value));
                        }
                        f = std::move(ptr);
                    }
                    else if (type == "Trace")
                    {
                        auto ptr = std::make_unique<TraceFilter>();
                        if (params.contains("threshold"))
                        {
                            int t = params.value("threshold", static_cast<int>(ptr->threshold));
                            if (t < 0) t = 0; if (t > 255) t = 255;
                            ptr->setThreshold(static_cast<uint8_t>(t));
                        }
                        f = std::move(ptr);
                    }
                    else if (type == "Simplify")
                    {
                        auto ptr = std::make_unique<SimplifyFilter>();
                        if (params.contains("toleranceMm"))
                        {
                            ptr->setTolerance(params.value("toleranceMm", ptr->toleranceMm));
                        }
                        f = std::move(ptr);
                    }
                    else
                    {
                        // Unknown filter type; skip
                    }

                    if (f)
                    {
                        size_t idx = e.filterChain.addFilter(std::move(f));
                        e.filterChain.setFilterEnabled(idx, enabled);
                    }
                }
            }
                    // Move to avoid copying Entity (FilterChain copy does not copy filters)
                    model.entities[e.id] = std::move(e);
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
