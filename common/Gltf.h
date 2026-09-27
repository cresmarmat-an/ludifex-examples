// Writes a small glTF 2.0 file with its buffer embedded as a data URI.
//
// The examples generate their own assets, so the repository has no binary
// files and the loader is tested with real files.

#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace examples
{

inline std::string EncodeBase64(const std::vector<uint8_t>& bytes)
{
    static const char* Alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const uint32_t byte0 = bytes[i];
        const uint32_t byte1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0u;
        const uint32_t byte2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0u;
        const uint32_t triple = (byte0 << 16) | (byte1 << 8) | byte2;

        encoded.push_back(Alphabet[(triple >> 18) & 0x3F]);
        encoded.push_back(Alphabet[(triple >> 12) & 0x3F]);
        encoded.push_back((i + 1 < bytes.size()) ? Alphabet[(triple >> 6) & 0x3F] : '=');
        encoded.push_back((i + 2 < bytes.size()) ? Alphabet[triple & 0x3F] : '=');
    }

    return encoded;
}

// An octahedron: six vertices, eight faces, wound counter-clockwise when seen
// from outside. Distinct enough from a box or a sphere that you can tell at a
// glance the loader did its job.
// A sphere of `rings` by `rings * 2` quads: a few thousand triangles, which is
// enough that parsing it takes real time and a loading test has something to
// measure.
inline bool WriteGltfSphere(const std::string& path, int rings, float radius)
{
    rings = rings < 4 ? 4 : rings;
    const int segments = rings * 2;

    std::vector<float> positions;
    std::vector<uint16_t> indices;

    for (int ring = 0; ring <= rings; ++ring)
    {
        const float theta = static_cast<float>(ring) / static_cast<float>(rings) * 3.14159265358979f;
        const float y = std::cos(theta) * radius;
        const float across = std::sin(theta) * radius;

        for (int segment = 0; segment <= segments; ++segment)
        {
            const float phi =
                static_cast<float>(segment) / static_cast<float>(segments) * 6.28318530717959f;
            positions.push_back(std::cos(phi) * across);
            positions.push_back(y);
            positions.push_back(std::sin(phi) * across);
        }
    }

    const int stride = segments + 1;
    for (int ring = 0; ring < rings; ++ring)
    {
        for (int segment = 0; segment < segments; ++segment)
        {
            const uint16_t a = static_cast<uint16_t>(ring * stride + segment);
            const uint16_t b = static_cast<uint16_t>(a + stride);

            indices.push_back(a);
            indices.push_back(static_cast<uint16_t>(a + 1));
            indices.push_back(b);

            indices.push_back(static_cast<uint16_t>(a + 1));
            indices.push_back(static_cast<uint16_t>(b + 1));
            indices.push_back(b);
        }
    }

    std::vector<uint8_t> buffer;
    const auto* positionBytes = reinterpret_cast<const uint8_t*>(positions.data());
    buffer.insert(buffer.end(), positionBytes, positionBytes + positions.size() * sizeof(float));

    // Accessor offsets must be aligned to their component size.
    while (buffer.size() % 4 != 0)
    {
        buffer.push_back(0);
    }

    const size_t indexOffset = buffer.size();
    const auto* indexBytes = reinterpret_cast<const uint8_t*>(indices.data());
    buffer.insert(buffer.end(), indexBytes, indexBytes + indices.size() * sizeof(uint16_t));

    const std::string encoded = EncodeBase64(buffer);
    const std::string minimum = "[" + std::to_string(-radius) + "," + std::to_string(-radius) + "," +
                                std::to_string(-radius) + "]";
    const std::string maximum = "[" + std::to_string(radius) + "," + std::to_string(radius) + "," +
                                std::to_string(radius) + "]";

    std::string json;
    json += "{\n";
    json += "  \"asset\": { \"version\": \"2.0\", \"generator\": \"ludifex examples\" },\n";
    json += "  \"scene\": 0,\n";
    json += "  \"scenes\": [ { \"nodes\": [ 0 ] } ],\n";
    json += "  \"nodes\": [ { \"mesh\": 0 } ],\n";
    json += "  \"meshes\": [ { \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, "
            "\"indices\": 1 } ] } ],\n";
    json += "  \"buffers\": [ { \"byteLength\": " + std::to_string(buffer.size()) +
            ", \"uri\": \"data:application/octet-stream;base64," + encoded + "\" } ],\n";
    json += "  \"bufferViews\": [\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": " +
            std::to_string(positions.size() * sizeof(float)) + ", \"target\": 34962 },\n";
    json += "    { \"buffer\": 0, \"byteOffset\": " + std::to_string(indexOffset) +
            ", \"byteLength\": " + std::to_string(indices.size() * sizeof(uint16_t)) +
            ", \"target\": 34963 }\n";
    json += "  ],\n";
    json += "  \"accessors\": [\n";
    json += "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": " +
            std::to_string(positions.size() / 3) + ", \"type\": \"VEC3\", \"min\": " + minimum +
            ", \"max\": " + maximum + " },\n";
    json += "    { \"bufferView\": 1, \"componentType\": 5123, \"count\": " +
            std::to_string(indices.size()) + ", \"type\": \"SCALAR\" }\n";
    json += "  ]\n";
    json += "}\n";

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    file << json;
    return file.good();
}

inline bool WriteGltfOctahedron(const std::string& path, float radius = 0.6f)
{
    const float positions[6][3] = {
        { 0.0f, radius, 0.0f },  { 0.0f, -radius, 0.0f }, { radius, 0.0f, 0.0f },
        { -radius, 0.0f, 0.0f }, { 0.0f, 0.0f, radius },  { 0.0f, 0.0f, -radius },
    };

    const uint16_t indices[24] = {
        0, 4, 2, 0, 2, 5, 0, 5, 3, 0, 3, 4,
        1, 2, 4, 1, 5, 2, 1, 3, 5, 1, 4, 3,
    };

    std::vector<uint8_t> buffer;

    const uint8_t* positionBytes = reinterpret_cast<const uint8_t*>(positions);
    buffer.insert(buffer.end(), positionBytes, positionBytes + sizeof(positions));

    const size_t indexOffset = buffer.size();
    const uint8_t* indexBytes = reinterpret_cast<const uint8_t*>(indices);
    buffer.insert(buffer.end(), indexBytes, indexBytes + sizeof(indices));

    const std::string encoded = EncodeBase64(buffer);

    // POSITION accessors must declare min and max; cgltf enforces it and so
    // does every other conforming loader.
    const std::string minimum = "[" + std::to_string(-radius) + "," + std::to_string(-radius) +
                                "," + std::to_string(-radius) + "]";
    const std::string maximum = "[" + std::to_string(radius) + "," + std::to_string(radius) + "," +
                                std::to_string(radius) + "]";

    std::string json;
    json += "{\n";
    json += "  \"asset\": { \"version\": \"2.0\", \"generator\": \"ludifex examples\" },\n";
    json += "  \"scene\": 0,\n";
    json += "  \"scenes\": [ { \"nodes\": [ 0 ] } ],\n";
    json += "  \"nodes\": [ { \"mesh\": 0 } ],\n";
    json += "  \"meshes\": [ { \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, "
            "\"indices\": 1 } ] } ],\n";
    json += "  \"buffers\": [ { \"byteLength\": " + std::to_string(buffer.size()) +
            ", \"uri\": \"data:application/octet-stream;base64," + encoded + "\" } ],\n";
    json += "  \"bufferViews\": [\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": " +
            std::to_string(sizeof(positions)) + ", \"target\": 34962 },\n";
    json += "    { \"buffer\": 0, \"byteOffset\": " + std::to_string(indexOffset) +
            ", \"byteLength\": " + std::to_string(sizeof(indices)) + ", \"target\": 34963 }\n";
    json += "  ],\n";
    json += "  \"accessors\": [\n";
    json += "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 6, \"type\": \"VEC3\", "
            "\"min\": " + minimum + ", \"max\": " + maximum + " },\n";
    json += "    { \"bufferView\": 1, \"componentType\": 5123, \"count\": 24, \"type\": "
            "\"SCALAR\" }\n";
    json += "  ]\n";
    json += "}\n";

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    return file.good();
}

} // namespace examples
