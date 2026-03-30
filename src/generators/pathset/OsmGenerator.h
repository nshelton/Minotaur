#pragma once

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <glog/logging.h>

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
// Shortbread tile schema layers exposed as toggles:
//   streets, buildings, water_polygons, water_lines, boundaries,
//   land_use, sites, streets_labels_only (excluded by default)

struct OsmGenerator : public GeneratorBase
{
	OsmGenerator()
	{
		// Location
		m_parameters["lat"]  = FilterParameter{"Latitude",  -85.0f,  85.0f, 40.7128f};  // NYC default
		m_parameters["lon"]  = FilterParameter{"Longitude", -180.0f, 180.0f, -74.006f};
		m_parameters["zoom"] = FilterParameter{"Zoom", 10.0f, 16.0f, 14.0f, FilterParameter::Int};

		// Output sizing
		m_parameters["size_mm"] = FilterParameter{"Size (mm)", 10.0f, 400.0f, 200.0f};

		// Tile grid: how many tiles to fetch (NxN centered on lat/lon)
		m_parameters["tile_count"] = FilterParameter{"Tiles NxN", 1.0f, 5.0f, 1.0f, FilterParameter::Int};

		// Layer toggles (0 = off, 1 = on)
		m_parameters["layer_streets"]         = FilterParameter{"Streets",         0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_buildings"]        = FilterParameter{"Buildings",       0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_water_polygons"]   = FilterParameter{"Water Polygons",  0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_water_lines"]      = FilterParameter{"Water Lines",     0.0f, 1.0f, 1.0f, FilterParameter::Bool};
		m_parameters["layer_boundaries"]       = FilterParameter{"Boundaries",      0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_land_use"]         = FilterParameter{"Land Use",        0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_sites"]            = FilterParameter{"Sites",           0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_ocean"]            = FilterParameter{"Ocean",           0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_ferries"]          = FilterParameter{"Ferries",         0.0f, 1.0f, 0.0f, FilterParameter::Bool};
		m_parameters["layer_public_transport"] = FilterParameter{"Public Transport",0.0f, 1.0f, 0.0f, FilterParameter::Bool};
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

		// Snapshot layer toggles
		LayerToggles toggles;
		toggles.streets         = m_parameters.at("layer_streets").value > 0.5f;
		toggles.buildings       = m_parameters.at("layer_buildings").value > 0.5f;
		toggles.waterPolygons   = m_parameters.at("layer_water_polygons").value > 0.5f;
		toggles.waterLines      = m_parameters.at("layer_water_lines").value > 0.5f;
		toggles.boundaries      = m_parameters.at("layer_boundaries").value > 0.5f;
		toggles.landUse         = m_parameters.at("layer_land_use").value > 0.5f;
		toggles.sites           = m_parameters.at("layer_sites").value > 0.5f;
		toggles.ocean           = m_parameters.at("layer_ocean").value > 0.5f;
		toggles.ferries         = m_parameters.at("layer_ferries").value > 0.5f;
		toggles.publicTransport = m_parameters.at("layer_public_transport").value > 0.5f;

		m_worker = std::thread([this, lat, lon, zoom, sizeMm, tileN, toggles]()
		{
			doGenerate(lat, lon, zoom, sizeMm, tileN, toggles);
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
		bool streets = true, buildings = true, waterPolygons = true, waterLines = true;
		bool boundaries = false, landUse = false, sites = false, ocean = false;
		bool ferries = false, publicTransport = false;
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
		if (layerName == "streets" || layerName == "street_labels")  return t.streets;
		if (layerName == "buildings")       return t.buildings;
		if (layerName == "water_polygons")  return t.waterPolygons;
		if (layerName == "water_lines")     return t.waterLines;
		if (layerName == "boundaries")      return t.boundaries;
		if (layerName == "land" || layerName == "land_use" || layerName == "landuse")
			return t.landUse;
		if (layerName == "sites")           return t.sites;
		if (layerName == "ocean")           return t.ocean;
		if (layerName == "ferries")         return t.ferries;
		if (layerName == "public_transport") return t.publicTransport;
		return false;
	}

	// ── Background work ─────────────────────────────────────────────────

	void doGenerate(float lat, float lon, int zoom, float sizeMm, int tileN,
	                const LayerToggles &toggles)
	{
		PathSet ps;
		ps.paths.clear();

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

				std::string url = fmt::format(
					"https://tiles.openfreemap.org/tiles/bright/{}/{}/{}.pbf",
					zoom, tx, ty);

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

		setStatus(fmt::format("Done - {} paths from {} tiles", ps.paths.size(), tilesDone));
		LOG(INFO) << "OSM generator: " << ps.paths.size() << " paths from " << tilesDone << " tiles";

		{
			std::lock_guard<std::mutex> lock(m_resultMutex);
			m_result = std::move(ps);
		}
		m_ready.store(true);
	}
};
