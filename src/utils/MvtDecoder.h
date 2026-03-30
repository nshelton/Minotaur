#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Minimal Mapbox Vector Tile (MVT) decoder.
// Decodes protobuf wire format just enough to parse the MVT spec v2.1.
// No external protobuf dependency required.

namespace mvt
{

// ── Protobuf wire helpers ───────────────────────────────────────────────

struct PbReader
{
	const uint8_t *ptr;
	const uint8_t *end;

	PbReader() : ptr(nullptr), end(nullptr) {}
	PbReader(const uint8_t *data, size_t len) : ptr(data), end(data + len) {}

	bool ok() const { return ptr < end; }

	uint64_t readVarint()
	{
		uint64_t val = 0;
		int shift = 0;
		while (ptr < end)
		{
			uint8_t b = *ptr++;
			val |= (uint64_t(b & 0x7F)) << shift;
			if ((b & 0x80) == 0) break;
			shift += 7;
			if (shift > 63) break;
		}
		return val;
	}

	int64_t readSVarint()
	{
		uint64_t v = readVarint();
		return int64_t((v >> 1) ^ -(v & 1));
	}

	uint32_t readFixed32()
	{
		uint32_t v = 0;
		if (ptr + 4 <= end) { std::memcpy(&v, ptr, 4); ptr += 4; }
		return v;
	}

	uint64_t readFixed64()
	{
		uint64_t v = 0;
		if (ptr + 8 <= end) { std::memcpy(&v, ptr, 8); ptr += 8; }
		return v;
	}

	float readFloat()
	{
		uint32_t bits = readFixed32();
		float f;
		std::memcpy(&f, &bits, 4);
		return f;
	}

	double readDouble()
	{
		uint64_t bits = readFixed64();
		double d;
		std::memcpy(&d, &bits, 8);
		return d;
	}

	PbReader readMessage()
	{
		uint64_t len = readVarint();
		if (ptr + len > end) len = end - ptr;
		PbReader sub(ptr, len);
		ptr += len;
		return sub;
	}

	std::string readString()
	{
		uint64_t len = readVarint();
		if (ptr + len > end) len = end - ptr;
		std::string s(reinterpret_cast<const char *>(ptr), len);
		ptr += len;
		return s;
	}

	std::vector<uint8_t> readBytes()
	{
		uint64_t len = readVarint();
		if (ptr + len > end) len = end - ptr;
		std::vector<uint8_t> b(ptr, ptr + len);
		ptr += len;
		return b;
	}

	// Read a packed repeated uint32 field
	std::vector<uint32_t> readPackedUint32()
	{
		uint64_t len = readVarint();
		if (ptr + len > end) len = end - ptr;
		const uint8_t *subEnd = ptr + len;
		std::vector<uint32_t> result;
		while (ptr < subEnd)
		{
			result.push_back(static_cast<uint32_t>(readVarint()));
		}
		return result;
	}

	// Skip a field by wire type
	void skip(int wireType)
	{
		switch (wireType)
		{
		case 0: readVarint(); break;
		case 1: ptr += 8; break;
		case 2: { uint64_t len = readVarint(); ptr += len; } break;
		case 5: ptr += 4; break;
		default: ptr = end; break;
		}
		if (ptr > end) ptr = end;
	}

	// Read field tag: returns (field_number, wire_type)
	std::pair<int, int> readTag()
	{
		uint64_t v = readVarint();
		return {int(v >> 3), int(v & 0x7)};
	}
};

// ── MVT data structures ─────────────────────────────────────────────────

enum GeomType { UNKNOWN = 0, POINT = 1, LINESTRING = 2, POLYGON = 3 };

struct Value
{
	enum Type { STRING, FLOAT, DOUBLE, INT, UINT, SINT, BOOL } type;
	std::string stringVal;
	double numVal = 0;
	bool boolVal = false;

	std::string asString() const
	{
		if (type == STRING) return stringVal;
		if (type == BOOL) return boolVal ? "true" : "false";
		return std::to_string(numVal);
	}
};

struct Feature
{
	uint64_t id = 0;
	GeomType geomType = UNKNOWN;
	std::vector<uint32_t> tags;
	std::vector<uint32_t> geometry;
};

struct Layer
{
	std::string name;
	uint32_t extent = 4096;
	uint32_t version = 2;
	std::vector<std::string> keys;
	std::vector<Value> values;
	std::vector<Feature> features;

	// Look up a tag key/value pair by index into the tags array
	std::string getTag(const Feature &f, const std::string &key) const
	{
		for (size_t i = 0; i + 1 < f.tags.size(); i += 2)
		{
			uint32_t ki = f.tags[i];
			uint32_t vi = f.tags[i + 1];
			if (ki < keys.size() && keys[ki] == key && vi < values.size())
				return values[vi].asString();
		}
		return "";
	}
};

struct Tile
{
	std::vector<Layer> layers;
};

// ── Geometry decoding ───────────────────────────────────────────────────

struct Point { double x, y; };
using Ring = std::vector<Point>;
using Geometry = std::vector<Ring>;  // For lines: each ring is a linestring
                                     // For polygons: exterior + holes

inline int32_t zigzagDecode(uint32_t n)
{
	return int32_t((n >> 1) ^ -(n & 1));
}

// Decode MVT geometry commands into rings of (tile-local) points.
// Coordinates are in tile-local integer space (0..extent).
inline Geometry decodeGeometry(const std::vector<uint32_t> &geom, GeomType type)
{
	Geometry result;
	int32_t cx = 0, cy = 0;
	size_t i = 0;

	while (i < geom.size())
	{
		uint32_t cmdInt = geom[i++];
		uint32_t cmd = cmdInt & 0x7;
		uint32_t count = cmdInt >> 3;

		if (cmd == 1) // MoveTo
		{
			for (uint32_t j = 0; j < count && i + 1 < geom.size(); ++j)
			{
				cx += zigzagDecode(geom[i++]);
				cy += zigzagDecode(geom[i++]);
				result.push_back(Ring());
				result.back().push_back({double(cx), double(cy)});
			}
		}
		else if (cmd == 2) // LineTo
		{
			for (uint32_t j = 0; j < count && i + 1 < geom.size(); ++j)
			{
				cx += zigzagDecode(geom[i++]);
				cy += zigzagDecode(geom[i++]);
				if (!result.empty())
					result.back().push_back({double(cx), double(cy)});
			}
		}
		else if (cmd == 7) // ClosePath
		{
			if (!result.empty() && !result.back().empty())
				result.back().push_back(result.back().front());
		}
	}
	return result;
}

// ── Decoding functions ──────────────────────────────────────────────────

inline Value decodeValue(PbReader &r)
{
	Value v;
	v.type = Value::STRING;
	while (r.ok())
	{
		auto [field, wire] = r.readTag();
		switch (field)
		{
		case 1: v.stringVal = r.readString(); v.type = Value::STRING; break;
		case 2: v.numVal = r.readFloat(); v.type = Value::FLOAT; break;
		case 3: v.numVal = r.readDouble(); v.type = Value::DOUBLE; break;
		case 4: v.numVal = double(int64_t(r.readVarint())); v.type = Value::INT; break;
		case 5: v.numVal = double(r.readVarint()); v.type = Value::UINT; break;
		case 6: v.numVal = double(r.readSVarint()); v.type = Value::SINT; break;
		case 7: v.boolVal = r.readVarint() != 0; v.type = Value::BOOL; break;
		default: r.skip(wire); break;
		}
	}
	return v;
}

inline Feature decodeFeature(PbReader &r)
{
	Feature f;
	while (r.ok())
	{
		auto [field, wire] = r.readTag();
		switch (field)
		{
		case 1: f.id = r.readVarint(); break;
		case 2: f.tags = r.readPackedUint32(); break;
		case 3: f.geomType = GeomType(r.readVarint()); break;
		case 4: f.geometry = r.readPackedUint32(); break;
		default: r.skip(wire); break;
		}
	}
	return f;
}

inline Layer decodeLayer(PbReader &r)
{
	Layer layer;
	while (r.ok())
	{
		auto [field, wire] = r.readTag();
		switch (field)
		{
		case 1: layer.name = r.readString(); break;
		case 2: { auto sub = r.readMessage(); layer.features.push_back(decodeFeature(sub)); } break;
		case 3: layer.keys.push_back(r.readString()); break;
		case 4: { auto sub = r.readMessage(); layer.values.push_back(decodeValue(sub)); } break;
		case 5: layer.extent = static_cast<uint32_t>(r.readVarint()); break;
		case 15: layer.version = static_cast<uint32_t>(r.readVarint()); break;
		default: r.skip(wire); break;
		}
	}
	return layer;
}

inline Tile decodeTile(const uint8_t *data, size_t len)
{
	Tile tile;
	PbReader r(data, len);
	while (r.ok())
	{
		auto [field, wire] = r.readTag();
		if (field == 3 && wire == 2)
		{
			auto sub = r.readMessage();
			tile.layers.push_back(decodeLayer(sub));
		}
		else
		{
			r.skip(wire);
		}
	}
	return tile;
}

inline Tile decodeTile(const std::vector<uint8_t> &data)
{
	return decodeTile(data.data(), data.size());
}

} // namespace mvt
