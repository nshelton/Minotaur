#pragma once

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <glog/logging.h>

#include <nlohmann/json.hpp>

#include "core/Core.h"
#include "generators/GeneratorBase.h"
#include "filters/Filter.h"
#include "filters/Types.h"
#include "utils/HttpFetcher.h"
#include "utils/MvtDecoder.h"

// OpenStreetMap vector tile generator.
//
// Fetches MVT tiles from OpenFreeMap (free, no API key) for a given lat/lon
// and zoom level, decodes the protobuf geometry, and converts it to a PathSet
// in millimetre coordinates suitable for pen plotting.
//
// This is an async generator: tile fetching happens on a background thread so
// the UI remains responsive.
//
// OpenMapTiles schema layers exposed as toggles:
//   transportation, building, water, waterway, boundary,
//   landuse, landcover, park, aeroway

struct OsmGenerator : public GeneratorBase
{
	OsmGenerator()
	{
		// Location
		m_parameters["lat"]  = FilterParameter{"Latitude",  -85.0f,  85.0f, 40.7128f, FilterParameter::Precise};  // NYC default
		m_parameters["lon"]  = FilterParameter{"Longitude", -180.0f, 180.0f, -74.006f, FilterParameter::Precise};
		m_parameters["zoom"] = FilterParameter{"Zoom", 10.0f, 16.0f, 14.0f, FilterParameter::Int};

		// Output sizing
		m_parameters["size_mm"] = FilterParameter{"Size (mm)", 10.0f, 400.0f, 200.0f};

		// Tile grid: how many tiles to fetch (NxN centered on lat/lon)
		m_parameters["tile_count"] = FilterParameter{"Tiles NxN", 1.0f, 5.0f, 1.0f, FilterParameter::Int};

		// Clip box (0 width/height = disabled)
		m_parameters["clip_offset_x"] = FilterParameter{"Clip Offset X (mm)", -200.0f, 200.0f, 0.0f};
		m_parameters["clip_offset_y"] = FilterParameter{"Clip Offset Y (mm)", -200.0f, 200.0f, 0.0f};
		m_parameters["clip_width"]    = FilterParameter{"Clip Width (mm)",      0.0f, 400.0f, 0.0f};
		m_parameters["clip_height"]   = FilterParameter{"Clip Height (mm)",     0.0f, 400.0f, 0.0f};

		// Layer toggles (0 = off, 1 = on) — OpenMapTiles schema
		m_parameters["layer_transportation"]   = FilterParameter{"Transportation",  0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_building"]         = FilterParameter{"Buildings",       0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_water"]            = FilterParameter{"Water",           0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_waterway"]         = FilterParameter{"Waterways",       0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_boundary"]         = FilterParameter{"Boundaries",      0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_landuse"]          = FilterParameter{"Land Use",        0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_landcover"]        = FilterParameter{"Land Cover",      0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_park"]             = FilterParameter{"Parks",           0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_aeroway"]          = FilterParameter{"Aeroways",        0.0f, 1.0f, 0.0f, FilterParameter::Bool};
	}

	~OsmGenerator() override
	{
		m_cancel.store(true);
		if (m_worker.joinable()) m_worker.join();
	}

	const char *name() const override { return "OSM Tiles"; }
	LayerKind outputKind() const override { return LayerKind::PathSet; }
	uint64_t paramVersion() const override { return m_version.load(); }

	// ── Async interface ─────────────────────────────────────────────────
	bool isAsync() const override { return true; }

	void startGenerate() override
	{
		// Cancel any in-flight work
		m_cancel.store(true);
		if (m_worker.joinable()) m_worker.join();

		m_ready.store(false);
		m_cancel.store(false);

		// Snapshot parameters for the worker thread
		const float lat      = m_parameters.at("lat").value;
		const float lon      = m_parameters.at("lon").value;
		const int   zoom     = static_cast<int>(std::lround(m_parameters.at("zoom").value));
		const float sizeMm   = m_parameters.at("size_mm").value;
		const int   tileN    = static_cast<int>(std::lround(m_parameters.at("tile_count").value));
		const float clipOx   = m_parameters.at("clip_offset_x").value;
		const float clipOy   = m_parameters.at("clip_offset_y").value;
		const float clipW    = m_parameters.at("clip_width").value;
		const float clipH    = m_parameters.at("clip_height").value;

		// Snapshot layer toggles
		LayerToggles toggles;
		toggles.transportation  = m_parameters.at("layer_transportation").value > 0.5f;
		toggles.building        = m_parameters.at("layer_building").value > 0.5f;
		toggles.water           = m_parameters.at("layer_water").value > 0.5f;
		toggles.waterway        = m_parameters.at("layer_waterway").value > 0.5f;
		toggles.boundary        = m_parameters.at("layer_boundary").value > 0.5f;
		toggles.landuse         = m_parameters.at("layer_landuse").value > 0.5f;
		toggles.landcover       = m_parameters.at("layer_landcover").value > 0.5f;
		toggles.park            = m_parameters.at("layer_park").value > 0.5f;
		toggles.aeroway         = m_parameters.at("layer_aeroway").value > 0.5f;

		m_worker = std::thread([this, lat, lon, zoom, sizeMm, tileN, clipOx, clipOy, clipW, clipH, toggles]()
		{
			doGenerate(lat, lon, zoom, sizeMm, tileN, clipOx, clipOy, clipW, clipH, toggles);
		});
	}

	bool isReady() const override { return m_ready.load(); }

	void collectResult(LayerPtr &out) override
	{
		if (m_worker.joinable()) m_worker.join();

		std::lock_guard<std::mutex> lock(m_resultMutex);
		auto ps = std::make_shared<PathSet>(std::move(m_result));
		out = std::static_pointer_cast<ILayerData>(ps);
		m_ready.store(false);
	}

	std::unique_ptr<GeneratorBase> clone() const override
	{
		auto copy = std::make_unique<OsmGenerator>();
		for (const auto &kv : m_parameters)
			copy->setParameter(kv.first, kv.second.value);
		for (const auto &kv : m_stringParameters)
			copy->setStringParameter(kv.first, kv.second);
		return copy;
	}

	// Status string for UI display
	std::string statusText() const
	{
		std::lock_guard<std::mutex> lock(m_statusMutex);
		return m_statusText;
	}

private:
	struct LayerToggles
	{
		bool transportation = true, building = true, water = true, waterway = true;
		bool boundary = false, landuse = false, landcover = false, park = false;
		bool aeroway = false;
	};

	std::thread m_worker;
	std::atomic<bool> m_ready{false};
	std::atomic<bool> m_cancel{false};

	mutable std::mutex m_resultMutex;
	PathSet m_result;

	mutable std::mutex m_statusMutex;
	mutable std::string m_statusText;

	void setStatus(const std::string &s) const
	{
		std::lock_guard<std::mutex> lock(m_statusMutex);
		m_statusText = s;
	}

	// ── Tile coordinate math ────────────────────────────────────────────

	static void latLonToTile(double lat, double lon, int z, int &tx, int &ty)
	{
		double n = std::pow(2.0, z);
		tx = static_cast<int>(std::floor((lon + 180.0) / 360.0 * n));
		double latRad = lat * M_PI / 180.0;
		ty = static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n));
		int maxTile = static_cast<int>(n) - 1;
		tx = std::max(0, std::min(tx, maxTile));
		ty = std::max(0, std::min(ty, maxTile));
	}

	// Returns the bounding box of a tile in Web Mercator projected coordinates (metres).
	// We use Mercator projection so geometry stays conformal.
	struct TileBounds { double x0, y0, x1, y1; }; // in Mercator metres

	static TileBounds tileBoundsMercator(int tx, int ty, int z)
	{
		double n = std::pow(2.0, z);
		// Mercator world extent ≈ 20037508.34m in each direction
		constexpr double worldExtent = 20037508.342789244;
		double tileSize = 2.0 * worldExtent / n;

		double x0 = -worldExtent + tx * tileSize;
		double x1 = x0 + tileSize;
		// Y is flipped: tile y=0 is north
		double y0 = worldExtent - (ty + 1) * tileSize;
		double y1 = y0 + tileSize;
		return {x0, y0, x1, y1};
	}

	// ── Layer name matching ─────────────────────────────────────────────

	static bool isLayerEnabled(const std::string &layerName, const LayerToggles &t)
	{
		if (layerName == "transportation" || layerName == "transportation_name")
			return t.transportation;
		if (layerName == "building")        return t.building;
		if (layerName == "water")           return t.water;
		if (layerName == "waterway")        return t.waterway;
		if (layerName == "boundary")        return t.boundary;
		if (layerName == "landuse")         return t.landuse;
		if (layerName == "landcover")       return t.landcover;
		if (layerName == "park")            return t.park;
		if (layerName == "aeroway")         return t.aeroway;
		return false;
	}

	// ── Background work ─────────────────────────────────────────────────

	// Fetch the TileJSON to discover the current tile URL template.
	// Returns empty string on failure.
	static std::string discoverTileUrlTemplate()
	{
		auto resp = HttpFetcher::get("https://tiles.openfreemap.org/planet");
		if (!resp.ok())
		{
			LOG(ERROR) << "Failed to fetch TileJSON: " << resp.error;
			return {};
		}

		try
		{
			auto j = nlohmann::json::parse(resp.data.begin(), resp.data.end());
			auto &tiles = j.at("tiles");
			if (tiles.is_array() && !tiles.empty())
				return tiles[0].get<std::string>();
		}
		catch (const std::exception &e)
		{
			LOG(ERROR) << "Failed to parse TileJSON: " << e.what();
		}
		return {};
	}

	// ── Line clipping (Cohen-Sutherland) ────────────────────────────────

	enum OutCode { INSIDE = 0, LEFT = 1, RIGHT = 2, BOTTOM = 4, TOP = 8 };

	static int computeOutCode(float x, float y, float xmin, float ymin, float xmax, float ymax)
	{
		int code = INSIDE;
		if      (x < xmin) code |= LEFT;
		else if (x > xmax) code |= RIGHT;
		if      (y < ymin) code |= BOTTOM;
		else if (y > ymax) code |= TOP;
		return code;
	}

	// Clip segment (x0,y0)-(x1,y1) to the rectangle. Returns false if fully outside.
	static bool clipSegment(float &x0, float &y0, float &x1, float &y1,
	                         float xmin, float ymin, float xmax, float ymax)
	{
		int code0 = computeOutCode(x0, y0, xmin, ymin, xmax, ymax);
		int code1 = computeOutCode(x1, y1, xmin, ymin, xmax, ymax);

		while (true)
		{
			if (!(code0 | code1))       return true;   // both inside
			if (code0 & code1)          return false;  // both in same outside zone

			int codeOut = code0 ? code0 : code1;
			float x, y;

			if (codeOut & TOP)        { x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0); y = ymax; }
			else if (codeOut & BOTTOM) { x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0); y = ymin; }
			else if (codeOut & RIGHT)  { y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0); x = xmax; }
			else                       { y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0); x = xmin; }

			if (codeOut == code0) { x0 = x; y0 = y; code0 = computeOutCode(x0, y0, xmin, ymin, xmax, ymax); }
			else                  { x1 = x; y1 = y; code1 = computeOutCode(x1, y1, xmin, ymin, xmax, ymax); }
		}
	}

	// Clip a polyline to a rectangle, producing zero or more sub-paths.
	static void clipPath(const Path &in, float xmin, float ymin, float xmax, float ymax,
	                      std::vector<Path> &out)
	{
		if (in.points.size() < 2) return;

		// For closed paths, treat as a loop (add closing segment)
		size_t n = in.points.size();
		size_t segments = in.closed ? n : n - 1;

		Path current;
		current.closed = false; // clipped fragments are always open

		for (size_t i = 0; i < segments; ++i)
		{
			size_t j = (i + 1) % n;
			float x0 = in.points[i].x, y0 = in.points[i].y;
			float x1 = in.points[j].x, y1 = in.points[j].y;

			float cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1;
			if (clipSegment(cx0, cy0, cx1, cy1, xmin, ymin, xmax, ymax))
			{
				// If the clipped start doesn't continue from the current path, flush and start new
				if (!current.points.empty())
				{
					Vec2 &last = current.points.back();
					if (last.x != cx0 || last.y != cy0)
					{
						if (current.points.size() >= 2)
							out.push_back(std::move(current));
						current = Path{};
						current.closed = false;
					}
				}

				if (current.points.empty())
					current.points.push_back(Vec2{cx0, cy0});
				current.points.push_back(Vec2{cx1, cy1});
			}
			else
			{
				// Segment fully outside — flush current path
				if (current.points.size() >= 2)
					out.push_back(std::move(current));
				current = Path{};
				current.closed = false;
			}
		}

		if (current.points.size() >= 2)
			out.push_back(std::move(current));
	}

	static PathSet clipPathSet(const PathSet &ps, float ox, float oy, float clipW, float clipH)
	{
		float hw = clipW * 0.5f;
		float hh = clipH * 0.5f;

		PathSet result;
		for (const auto &path : ps.paths)
			clipPath(path, ox - hw, oy - hh, ox + hw, oy + hh, result.paths);
		return result;
	}

	// Expand a TileJSON URL template by replacing {z}, {x}, {y}.
	static std::string expandTileUrl(const std::string &tmpl, int z, int x, int y)
	{
		std::string url = tmpl;
		auto replace = [&](const std::string &token, int val) {
			auto pos = url.find(token);
			if (pos != std::string::npos)
				url.replace(pos, token.size(), std::to_string(val));
		};
		replace("{z}", z);
		replace("{x}", x);
		replace("{y}", y);
		return url;
	}

	void doGenerate(float lat, float lon, int zoom, float sizeMm, int tileN,
	                float clipOx, float clipOy, float clipW, float clipH,
	                const LayerToggles &toggles)
	{
		PathSet ps;
		ps.paths.clear();

		// Discover current tile URL from TileJSON
		setStatus("Discovering tile endpoint...");
		std::string tileUrlTemplate = discoverTileUrlTemplate();
		if (tileUrlTemplate.empty())
		{
			setStatus("Error: could not discover tile URL");
			return;
		}

		if (m_cancel.load()) { setStatus("Cancelled"); return; }

		// Determine center tile
		int cx, cy;
		latLonToTile(lat, lon, zoom, cx, cy);

		// Build list of tiles to fetch (NxN grid centered on cx,cy)
		int half = tileN / 2;
		int startX = cx - half;
		int startY = cy - half;

		// Compute the Mercator bounding box of all tiles combined
		auto topLeft = tileBoundsMercator(startX, startY, zoom);
		auto botRight = tileBoundsMercator(startX + tileN - 1, startY + tileN - 1, zoom);
		double totalMercX = botRight.x1 - topLeft.x0;
		double totalMercY = topLeft.y1 - botRight.y0;
		double maxMerc = std::max(totalMercX, totalMercY);
		if (maxMerc < 1e-6) maxMerc = 1.0;
		double scale = sizeMm / maxMerc;

		int tilesTotal = tileN * tileN;
		int tilesDone = 0;

		for (int dy = 0; dy < tileN && !m_cancel.load(); ++dy)
		{
			for (int dx = 0; dx < tileN && !m_cancel.load(); ++dx)
			{
				int tx = startX + dx;
				int ty = startY + dy;

				setStatus(fmt::format("Fetching tile {}/{} (z={} x={} y={})",
					tilesDone + 1, tilesTotal, zoom, tx, ty));

				std::string url = expandTileUrl(tileUrlTemplate, zoom, tx, ty);

				auto resp = HttpFetcher::get(url);
				if (!resp.ok())
				{
					LOG(WARNING) << "Failed to fetch tile " << url << ": " << resp.error;
					tilesDone++;
					continue;
				}

				// Decode the MVT tile
				auto tile = mvt::decodeTile(resp.data);

				// This tile's Mercator bounds
				auto tb = tileBoundsMercator(tx, ty, zoom);

				// Process each layer
				for (const auto &layer : tile.layers)
				{
					if (!isLayerEnabled(layer.name, toggles)) continue;

					double extent = layer.extent;

					for (const auto &feature : layer.features)
					{
						if (m_cancel.load()) break;

						// Only process linestrings and polygons (render as outlines)
						if (feature.geomType == mvt::POINT) continue;

						auto geom = mvt::decodeGeometry(feature.geometry, feature.geomType);

						for (const auto &ring : geom)
						{
							if (ring.size() < 2) continue;

							Path path;
							path.closed = (feature.geomType == mvt::POLYGON);

							for (const auto &pt : ring)
							{
								// Convert tile-local coords to Mercator
								double mercX = tb.x0 + (pt.x / extent) * (tb.x1 - tb.x0);
								double mercY = tb.y1 - (pt.y / extent) * (tb.y1 - tb.y0);

								// Convert to mm, centered on origin
								double mmX = (mercX - topLeft.x0 - totalMercX * 0.5) * scale;
								double mmY = (mercY - botRight.y0 - totalMercY * 0.5) * scale;

								path.points.push_back(Vec2{
									static_cast<float>(mmX),
									static_cast<float>(-mmY) // flip Y for screen coords
								});
							}

							// For closed polygons, don't duplicate the closing point
							// (the decoder adds it, but PathSet uses the closed flag)
							if (path.closed && path.points.size() > 1 &&
								path.points.front().x == path.points.back().x &&
								path.points.front().y == path.points.back().y)
							{
								path.points.pop_back();
							}

							if (path.points.size() >= 2)
								ps.paths.push_back(std::move(path));
						}
					}
				}

				tilesDone++;
			}
		}

		if (m_cancel.load())
		{
			setStatus("Cancelled");
			return;
		}

		// Clip to bounding box if enabled
		if (clipW > 0.0f && clipH > 0.0f)
		{
			setStatus("Clipping paths...");
			ps = clipPathSet(ps, clipOx, clipOy, clipW, clipH);
		}

		setStatus(fmt::format("Done - {} paths from {} tiles", ps.paths.size(), tilesDone));
		LOG(INFO) << "OSM generator: " << ps.paths.size() << " paths from " << tilesDone << " tiles";

		{
			std::lock_guard<std::mutex> lock(m_resultMutex);
			m_result = std::move(ps);
		}
		m_ready.store(true);
	}
};
