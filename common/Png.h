// Writes an RGBA image as a PNG file.
//
// The pixel data is stored in uncompressed deflate blocks to keep the writer
// short. Everything else (the signature, chunk CRCs, zlib header, Adler-32
// checksum, and row filter bytes) is standard PNG, so any decoder reads it
// normally.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace examples
{

namespace png_detail
{

inline uint32_t Crc(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu)
{
    for (size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc;
}

inline void PutBigEndian(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

inline void PutChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
{
    PutBigEndian(out, static_cast<uint32_t>(data.size()));

    std::vector<uint8_t> typed(type, type + 4);
    typed.insert(typed.end(), data.begin(), data.end());

    out.insert(out.end(), typed.begin(), typed.end());
    PutBigEndian(out, Crc(typed.data(), typed.size()) ^ 0xFFFFFFFFu);
}

} // namespace png_detail

// pixels holds width * height * 4 bytes, rows top to bottom, straight alpha.
inline bool WritePng(const std::string& path, int width, int height, const std::vector<uint8_t>& pixels)
{
    using namespace png_detail;

    if (width <= 0 || height <= 0 ||
        pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
    {
        return false;
    }

    // Every row is prefixed with filter type 0, meaning "none".
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y)
    {
        raw.push_back(0);
        const uint8_t* row = &pixels[static_cast<size_t>(y) * static_cast<size_t>(width) * 4];
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    // zlib: a header, stored deflate blocks of at most 65535 bytes, and the
    // Adler-32 of the uncompressed data.
    std::vector<uint8_t> zlib{ 0x78, 0x01 };
    size_t offset = 0;
    do
    {
        const size_t length = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + length == raw.size();

        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length & 0xFF));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length & 0xFF));
        zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));

        offset += length;
    } while (offset < raw.size());

    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : raw)
    {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    PutBigEndian(zlib, (b << 16) | a);

    std::vector<uint8_t> header;
    PutBigEndian(header, static_cast<uint32_t>(width));
    PutBigEndian(header, static_cast<uint32_t>(height));
    header.push_back(8); // bit depth
    header.push_back(6); // colour type: RGBA
    header.push_back(0); // compression
    header.push_back(0); // filter
    header.push_back(0); // interlace

    std::vector<uint8_t> file{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    PutChunk(file, "IHDR", header);
    PutChunk(file, "IDAT", zlib);
    PutChunk(file, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

} // namespace examples
