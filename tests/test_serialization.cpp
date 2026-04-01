#include <gtest/gtest.h>
#include <glog/logging.h>

#include <fstream>
#include <filesystem>
#include <cstring>

#include "utils/ImageCompression.h"
#include "utils/Serialization.h"
#include "Page.h"
#include "core/Core.h"
#include "filters/FilterRegistry.h"
#include "generators/GeneratorRegistry.h"
#include "generators/pathset/CircleGenerator.h"

// ===========================================================================
// ImageCompression: RLE round-trip
// ===========================================================================

TEST(ImageCompression, RleRoundTripUniform)
{
	std::vector<uint8_t> data(1000, 42);
	auto encoded = imagecompression::rleEncode(data);
	auto decoded = imagecompression::rleDecode(encoded);

	EXPECT_EQ(decoded.size(), data.size());
	EXPECT_EQ(decoded, data);
	// RLE should compress uniform data significantly
	EXPECT_LT(encoded.size(), data.size());
}

TEST(ImageCompression, RleRoundTripVarying)
{
	std::vector<uint8_t> data(256);
	for (size_t i = 0; i < 256; ++i)
		data[i] = static_cast<uint8_t>(i);

	auto encoded = imagecompression::rleEncode(data);
	auto decoded = imagecompression::rleDecode(encoded);

	EXPECT_EQ(decoded, data);
}

TEST(ImageCompression, RleRoundTripEmpty)
{
	std::vector<uint8_t> data;
	auto encoded = imagecompression::rleEncode(data);
	auto decoded = imagecompression::rleDecode(encoded);
	EXPECT_TRUE(decoded.empty());
}

TEST(ImageCompression, RleRoundTripSingleByte)
{
	std::vector<uint8_t> data = {99};
	auto encoded = imagecompression::rleEncode(data);
	auto decoded = imagecompression::rleDecode(encoded);
	EXPECT_EQ(decoded, data);
}

TEST(ImageCompression, RleRoundTripAlternating)
{
	std::vector<uint8_t> data;
	for (int i = 0; i < 100; ++i)
	{
		data.push_back(0);
		data.push_back(255);
	}
	auto encoded = imagecompression::rleEncode(data);
	auto decoded = imagecompression::rleDecode(encoded);
	EXPECT_EQ(decoded, data);
}

// ===========================================================================
// ImageCompression: Base64 round-trip
// ===========================================================================

TEST(ImageCompression, Base64RoundTrip)
{
	std::vector<uint8_t> data = {0, 1, 2, 127, 128, 254, 255};
	auto encoded = imagecompression::base64Encode(data);
	auto decoded = imagecompression::base64Decode(encoded);
	EXPECT_EQ(decoded, data);
}

TEST(ImageCompression, Base64RoundTripEmpty)
{
	std::vector<uint8_t> data;
	auto encoded = imagecompression::base64Encode(data);
	auto decoded = imagecompression::base64Decode(encoded);
	EXPECT_TRUE(decoded.empty());
}

TEST(ImageCompression, Base64RoundTripLargeData)
{
	std::vector<uint8_t> data(10000);
	for (size_t i = 0; i < data.size(); ++i)
		data[i] = static_cast<uint8_t>(i % 256);

	auto encoded = imagecompression::base64Encode(data);
	auto decoded = imagecompression::base64Decode(encoded);
	EXPECT_EQ(decoded, data);
}

// ===========================================================================
// ImageCompression: CompressToString / DecompressFromString (RLE + Base64)
// ===========================================================================

TEST(ImageCompression, CompressDecompressRoundTrip)
{
	std::vector<uint8_t> data(4096, 128);
	// Sprinkle some variety
	for (size_t i = 0; i < data.size(); i += 7)
		data[i] = static_cast<uint8_t>(i % 256);

	auto compressed = imagecompression::compressToString(data);
	auto decompressed = imagecompression::decompressFromString(compressed);

	EXPECT_EQ(decompressed, data);
}

TEST(ImageCompression, CompressDecompressEmpty)
{
	std::vector<uint8_t> data;
	auto compressed = imagecompression::compressToString(data);
	auto decompressed = imagecompression::decompressFromString(compressed);
	EXPECT_TRUE(decompressed.empty());
}

// ===========================================================================
// ImageCompression: Float encoding round-trip
// ===========================================================================

TEST(ImageCompression, FloatEncodeDecodeRoundTrip)
{
	std::vector<float> data = {0.0f, 1.0f, -1.0f, 3.14159f, 1e10f, -1e-10f};
	auto encoded = imagecompression::encodeFloats(data);
	auto decoded = imagecompression::decodeFloats(encoded);

	ASSERT_EQ(decoded.size(), data.size());
	for (size_t i = 0; i < data.size(); ++i)
	{
		EXPECT_FLOAT_EQ(decoded[i], data[i]);
	}
}

TEST(ImageCompression, FloatEncodeDecodeEmpty)
{
	std::vector<float> data;
	auto encoded = imagecompression::encodeFloats(data);
	auto decoded = imagecompression::decodeFloats(encoded);
	EXPECT_TRUE(decoded.empty());
}

TEST(ImageCompression, FloatEncodeDecodeLargeArray)
{
	std::vector<float> data(1000);
	for (size_t i = 0; i < data.size(); ++i)
		data[i] = static_cast<float>(i) * 0.001f;

	auto encoded = imagecompression::encodeFloats(data);
	auto decoded = imagecompression::decodeFloats(encoded);

	ASSERT_EQ(decoded.size(), data.size());
	for (size_t i = 0; i < data.size(); ++i)
	{
		EXPECT_FLOAT_EQ(decoded[i], data[i]);
	}
}

// ===========================================================================
// PageModel save/load round-trip (static entities)
// ===========================================================================

class SerializationRoundTrip : public ::testing::Test
{
protected:
	void SetUp() override
	{
		FilterRegistry::initDefaults();
		GeneratorRegistry::initDefaults();
		m_tempFile = std::filesystem::temp_directory_path() / "minotaur_test_page.json";
	}

	void TearDown() override
	{
		std::filesystem::remove(m_tempFile);
	}

	std::filesystem::path m_tempFile;
};

TEST_F(SerializationRoundTrip, EmptyPageModel)
{
	PageModel original;
	std::string err;

	ASSERT_TRUE(serialization::savePageModel(original, m_tempFile.string(), &err)) << err;

	PageModel loaded;
	ASSERT_TRUE(serialization::loadPageModel(loaded, m_tempFile.string(), &err)) << err;

	EXPECT_TRUE(loaded.entities.empty());
}

TEST_F(SerializationRoundTrip, PathSetEntity)
{
	PageModel original;
	PathSet ps;
	Path p;
	p.closed = true;
	p.points = {Vec2{1.0f, 2.0f}, Vec2{3.0f, 4.0f}, Vec2{5.0f, 6.0f}};
	ps.paths.push_back(std::move(p));

	int id = original.addEntity(ps, "TestPath");
	original.entities[id].visible = true;
	original.entities[id].color = Color{0.5f, 0.6f, 0.7f, 0.8f};

	std::string err;
	ASSERT_TRUE(serialization::savePageModel(original, m_tempFile.string(), &err)) << err;

	PageModel loaded;
	ASSERT_TRUE(serialization::loadPageModel(loaded, m_tempFile.string(), &err)) << err;

	ASSERT_EQ(loaded.entities.size(), 1u);
	const Entity &e = loaded.entities.begin()->second;
	EXPECT_EQ(e.name, "TestPath");
	EXPECT_TRUE(e.visible);
	EXPECT_NEAR(e.color.r, 0.5f, 0.01f);
	EXPECT_NEAR(e.color.g, 0.6f, 0.01f);

	const auto *loadedPs = e.pathset();
	ASSERT_NE(loadedPs, nullptr);
	ASSERT_EQ(loadedPs->paths.size(), 1u);
	EXPECT_TRUE(loadedPs->paths[0].closed);
	ASSERT_EQ(loadedPs->paths[0].points.size(), 3u);
	EXPECT_FLOAT_EQ(loadedPs->paths[0].points[0].x, 1.0f);
	EXPECT_FLOAT_EQ(loadedPs->paths[0].points[0].y, 2.0f);
}

TEST_F(SerializationRoundTrip, BitmapEntity)
{
	PageModel original;
	Bitmap bmp;
	bmp.width_px = 8;
	bmp.height_px = 6;
	bmp.pixel_size_mm = 0.5f;
	bmp.pixels.assign(48, 200);

	int id = original.addEntity(bmp, "TestBitmap");

	std::string err;
	ASSERT_TRUE(serialization::savePageModel(original, m_tempFile.string(), &err)) << err;

	PageModel loaded;
	ASSERT_TRUE(serialization::loadPageModel(loaded, m_tempFile.string(), &err)) << err;

	ASSERT_EQ(loaded.entities.size(), 1u);
	const Entity &e = loaded.entities.begin()->second;
	const auto *loadedBmp = e.bitmap();
	ASSERT_NE(loadedBmp, nullptr);
	EXPECT_EQ(loadedBmp->width_px, 8u);
	EXPECT_EQ(loadedBmp->height_px, 6u);
	EXPECT_FLOAT_EQ(loadedBmp->pixel_size_mm, 0.5f);
	ASSERT_EQ(loadedBmp->pixels.size(), 48u);
	EXPECT_EQ(loadedBmp->pixels[0], 200);
}

TEST_F(SerializationRoundTrip, MultipleEntities)
{
	PageModel original;

	PathSet ps;
	Path p;
	p.closed = false;
	p.points = {Vec2{0, 0}, Vec2{10, 10}};
	ps.paths.push_back(std::move(p));
	original.addEntity(ps, "Path1");

	PathSet ps2;
	Path p2;
	p2.closed = true;
	p2.points = {Vec2{0, 0}, Vec2{5, 0}, Vec2{5, 5}};
	ps2.paths.push_back(std::move(p2));
	original.addEntity(ps2, "Path2");

	std::string err;
	ASSERT_TRUE(serialization::savePageModel(original, m_tempFile.string(), &err)) << err;

	PageModel loaded;
	ASSERT_TRUE(serialization::loadPageModel(loaded, m_tempFile.string(), &err)) << err;

	EXPECT_EQ(loaded.entities.size(), 2u);
}

TEST_F(SerializationRoundTrip, GeneratedEntityPreservesGenerator)
{
	PageModel original;
	auto gen = std::make_unique<CircleGenerator>();
	gen->setParameter("radius_mm", 75.0f);
	gen->setParameter("segments", 32.0f);
	original.addGeneratedEntity(std::move(gen));

	std::string err;
	ASSERT_TRUE(serialization::savePageModel(original, m_tempFile.string(), &err)) << err;

	PageModel loaded;
	ASSERT_TRUE(serialization::loadPageModel(loaded, m_tempFile.string(), &err)) << err;

	ASSERT_EQ(loaded.entities.size(), 1u);
	const Entity &e = loaded.entities.begin()->second;

	// Generator should be restored
	ASSERT_NE(e.generator, nullptr);
	EXPECT_STREQ(e.generator->name(), "Circle");
	EXPECT_FLOAT_EQ(e.generator->m_parameters.at("radius_mm").value, 75.0f);
	EXPECT_FLOAT_EQ(e.generator->m_parameters.at("segments").value, 32.0f);

	// Payload should be regenerated
	const auto *ps = e.pathset();
	ASSERT_NE(ps, nullptr);
	EXPECT_GT(ps->paths.size(), 0u);
}

TEST_F(SerializationRoundTrip, LoadNonexistentFileFails)
{
	PageModel model;
	std::string err;
	EXPECT_FALSE(serialization::loadPageModel(model, "/nonexistent/path.json", &err));
	EXPECT_FALSE(err.empty());
}

TEST_F(SerializationRoundTrip, SaveToInvalidPathFails)
{
	PageModel model;
	std::string err;
	EXPECT_FALSE(serialization::savePageModel(model, "/nonexistent/dir/page.json", &err));
	EXPECT_FALSE(err.empty());
}

int main(int argc, char **argv)
{
	google::InitGoogleLogging(argv[0]);
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
