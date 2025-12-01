#include "utils/ImageCompression.h"

#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace imagecompression
{
    // Base64 encoding table
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string base64Encode(const std::vector<uint8_t> &data)
    {
        std::string ret;
        int i = 0;
        int j = 0;
        uint8_t char_array_3[3];
        uint8_t char_array_4[4];
        size_t in_len = data.size();
        const uint8_t *bytes_to_encode = data.data();

        while (in_len--)
        {
            char_array_3[i++] = *(bytes_to_encode++);
            if (i == 3)
            {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; i < 4; i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i)
        {
            for (j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

            for (j = 0; j < i + 1; j++)
                ret += base64_chars[char_array_4[j]];

            while ((i++ < 3))
                ret += '=';
        }

        return ret;
    }

    static inline bool is_base64(uint8_t c)
    {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }

    std::vector<uint8_t> base64Decode(const std::string &encoded_string)
    {
        size_t in_len = encoded_string.size();
        size_t i = 0;
        size_t j = 0;
        int in_ = 0;
        uint8_t char_array_4[4], char_array_3[3];
        std::vector<uint8_t> ret;

        while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_]))
        {
            char_array_4[i++] = encoded_string[in_];
            in_++;
            if (i == 4)
            {
                for (i = 0; i < 4; i++)
                {
                    char_array_4[i] = static_cast<uint8_t>(std::string(base64_chars).find(char_array_4[i]));
                }

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                for (i = 0; i < 3; i++)
                    ret.push_back(char_array_3[i]);
                i = 0;
            }
        }

        if (i)
        {
            for (j = 0; j < i; j++)
                char_array_4[j] = static_cast<uint8_t>(std::string(base64_chars).find(char_array_4[j]));

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

            for (j = 0; j < i - 1; j++)
                ret.push_back(char_array_3[j]);
        }

        return ret;
    }

    std::vector<uint8_t> rleEncode(const std::vector<uint8_t> &data)
    {
        if (data.empty())
            return {};

        std::vector<uint8_t> encoded;
        encoded.reserve(data.size() / 2); // Estimate

        size_t i = 0;
        while (i < data.size())
        {
            uint8_t value = data[i];
            uint32_t count = 1;

            // Count consecutive same values (max 2^32-1)
            while (i + count < data.size() && data[i + count] == value && count < 0xFFFFFFFFu)
            {
                ++count;
            }

            // Store count as 4 bytes (little-endian uint32_t)
            encoded.push_back(static_cast<uint8_t>(count & 0xFF));
            encoded.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
            encoded.push_back(static_cast<uint8_t>((count >> 16) & 0xFF));
            encoded.push_back(static_cast<uint8_t>((count >> 24) & 0xFF));

            // Store value
            encoded.push_back(value);

            i += count;
        }

        return encoded;
    }

    std::vector<uint8_t> rleDecode(const std::vector<uint8_t> &encoded)
    {
        std::vector<uint8_t> decoded;

        size_t i = 0;
        while (i + 4 < encoded.size())
        {
            // Read count (4 bytes, little-endian)
            uint32_t count = static_cast<uint32_t>(encoded[i]) |
                             (static_cast<uint32_t>(encoded[i + 1]) << 8) |
                             (static_cast<uint32_t>(encoded[i + 2]) << 16) |
                             (static_cast<uint32_t>(encoded[i + 3]) << 24);
            i += 4;

            // Read value
            uint8_t value = encoded[i];
            i += 1;

            // Decode run
            decoded.insert(decoded.end(), count, value);
        }

        return decoded;
    }

    std::string compressToString(const std::vector<uint8_t> &data)
    {
        std::vector<uint8_t> rle = rleEncode(data);
        return base64Encode(rle);
    }

    std::vector<uint8_t> decompressFromString(const std::string &compressed)
    {
        std::vector<uint8_t> rle = base64Decode(compressed);
        return rleDecode(rle);
    }

    std::string encodeFloats(const std::vector<float> &data)
    {
        // Convert floats to raw bytes
        std::vector<uint8_t> bytes(data.size() * sizeof(float));
        std::memcpy(bytes.data(), data.data(), bytes.size());
        return base64Encode(bytes);
    }

    std::vector<float> decodeFloats(const std::string &encoded)
    {
        std::vector<uint8_t> bytes = base64Decode(encoded);
        std::vector<float> floats(bytes.size() / sizeof(float));
        std::memcpy(floats.data(), bytes.data(), bytes.size());
        return floats;
    }
}
