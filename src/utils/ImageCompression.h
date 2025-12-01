#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace imagecompression
{
    // Run-length encode byte data
    // Format: [count, value, count, value, ...] where count is uint32_t
    std::vector<uint8_t> rleEncode(const std::vector<uint8_t> &data);

    // Decode RLE data
    std::vector<uint8_t> rleDecode(const std::vector<uint8_t> &encoded);

    // Base64 encode binary data to string
    std::string base64Encode(const std::vector<uint8_t> &data);

    // Base64 decode string to binary data
    std::vector<uint8_t> base64Decode(const std::string &encoded);

    // Convenience: RLE + Base64 encode
    std::string compressToString(const std::vector<uint8_t> &data);

    // Convenience: Base64 + RLE decode
    std::vector<uint8_t> decompressFromString(const std::string &compressed);

    // Float array helpers (no RLE, just base64 for raw binary)
    std::string encodeFloats(const std::vector<float> &data);
    std::vector<float> decodeFloats(const std::string &encoded);
}
