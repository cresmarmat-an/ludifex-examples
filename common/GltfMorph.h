// Writes a glTF 2.0 file with morph targets and a clip that animates them, with
// its buffer embedded.
//
// It holds two panels. The left one, "face", has two shapes: "Bulge" pushes its
// middle out into a dome and "Stretch" pulls its top edge up. It starts with
// Bulge at a quarter, from the node's weights. The right one, "flag", has one
// shape, "Wave", and no weights, so it starts flat. The clip "breathe" animates
// the face's two weights over two seconds and does not touch the flag, which
// shows that a clip only drives the meshes it targets.

#pragma once

#include "Gltf.h"
#include "GltfSkinned.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace examples
{

inline bool WriteGltfMorphPanels(const std::string& path)
{
    constexpr int Cells = 12;
    constexpr int Side = Cells + 1;
    constexpr float Width = 1.6f;
    constexpr float Height = 1.6f;
    const size_t vertexCount = static_cast<size_t>(Side) * Side;

    // One grid serves both panels: in the XY plane, facing +Z.
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint16_t> indices;
    for (int row = 0; row < Side; ++row)
    {
        for (int column = 0; column < Side; ++column)
        {
            positions.push_back((static_cast<float>(column) / Cells - 0.5f) * Width);
            positions.push_back(static_cast<float>(row) / Cells * Height);
            positions.push_back(0.0f);
            normals.insert(normals.end(), { 0.0f, 0.0f, 1.0f });
        }
    }
    for (int row = 0; row < Cells; ++row)
    {
        for (int column = 0; column < Cells; ++column)
        {
            const uint16_t a = static_cast<uint16_t>(row * Side + column);
            const uint16_t b = static_cast<uint16_t>(a + 1);
            const uint16_t c = static_cast<uint16_t>(a + Side);
            const uint16_t d = static_cast<uint16_t>(c + 1);
            indices.insert(indices.end(), { a, b, c, b, d, c });
        }
    }

    // The shapes, as displacements from the grid.
    std::vector<float> bulge;
    std::vector<float> bulgeNormals;
    std::vector<float> stretch;
    std::vector<float> wave;
    std::vector<float> waveNormals;
    for (size_t vertex = 0; vertex < vertexCount; ++vertex)
    {
        const float x = positions[vertex * 3 + 0];
        const float y = positions[vertex * 3 + 1];

        // A dome of height 0.4 over a circle of radius 0.7 round the middle.
        const float dx = x;
        const float dy = y - Height * 0.5f;
        const float r2 = (dx * dx + dy * dy) / (0.7f * 0.7f);
        const float lift = r2 < 1.0f ? 0.4f * (1.0f - r2) : 0.0f;
        bulge.insert(bulge.end(), { 0.0f, 0.0f, lift });

        // The dome's normal, less the flat one.
        const float slopeX = r2 < 1.0f ? -0.8f * dx / (0.7f * 0.7f) : 0.0f;
        const float slopeY = r2 < 1.0f ? -0.8f * dy / (0.7f * 0.7f) : 0.0f;
        const float length = std::sqrt(slopeX * slopeX + slopeY * slopeY + 1.0f);
        bulgeNormals.insert(bulgeNormals.end(), { -slopeX / length, -slopeY / length, 1.0f / length - 1.0f });

        stretch.insert(stretch.end(), { 0.0f, 0.5f * (y / Height) * (y / Height), 0.0f });

        const float ripple = 0.18f * std::sin(x * 6.0f) * (y / Height);
        wave.insert(wave.end(), { 0.0f, 0.0f, ripple });
        const float waveSlope = 0.18f * 6.0f * std::cos(x * 6.0f) * (y / Height);
        const float waveLength = std::sqrt(waveSlope * waveSlope + 1.0f);
        waveNormals.insert(waveNormals.end(), { -waveSlope / waveLength, 0.0f, 1.0f / waveLength - 1.0f });
    }

    // "breathe": the face's two weights, per key, over two seconds.
    const std::vector<float> times = { 0.0f, 1.0f, 2.0f };
    const std::vector<float> weights = { 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f };

    std::vector<uint8_t> buffer;
    auto Add = [&](const std::vector<float>& values) {
        const size_t offset = buffer.size();
        detail::AppendFloats(buffer, values);
        detail::PadTo4(buffer);
        return offset;
    };

    const size_t positionOffset = Add(positions);
    const size_t normalOffset = Add(normals);
    const size_t indexOffset = buffer.size();
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(indices.data());
        buffer.insert(buffer.end(), bytes, bytes + indices.size() * sizeof(uint16_t));
        detail::PadTo4(buffer);
    }
    const size_t bulgeOffset = Add(bulge);
    const size_t bulgeNormalOffset = Add(bulgeNormals);
    const size_t stretchOffset = Add(stretch);
    const size_t waveOffset = Add(wave);
    const size_t waveNormalOffset = Add(waveNormals);
    const size_t timeOffset = Add(times);
    const size_t weightOffset = Add(weights);

    auto View = [](size_t offset, size_t bytes) {
        return "    { \"buffer\": 0, \"byteOffset\": " + std::to_string(offset) +
               ", \"byteLength\": " + std::to_string(bytes) + " }";
    };
    auto Vec3Accessor = [&](int view, bool bounds, const std::vector<float>& values) {
        std::string text = "    { \"bufferView\": " + std::to_string(view) +
                           ", \"componentType\": 5126, \"count\": " + std::to_string(vertexCount) +
                           ", \"type\": \"VEC3\"";
        if (bounds)
        {
            float minimum[3] = { 1e9f, 1e9f, 1e9f };
            float maximum[3] = { -1e9f, -1e9f, -1e9f };
            for (size_t vertex = 0; vertex < vertexCount; ++vertex)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], values[vertex * 3 + static_cast<size_t>(axis)]);
                    maximum[axis] = std::max(maximum[axis], values[vertex * 3 + static_cast<size_t>(axis)]);
                }
            }
            text += ", \"min\": " + detail::Floats({ minimum[0], minimum[1], minimum[2] }) +
                    ", \"max\": " + detail::Floats({ maximum[0], maximum[1], maximum[2] });
        }
        return text + " }";
    };

    std::string json;
    json += "{\n";
    json += "  \"asset\": { \"version\": \"2.0\", \"generator\": \"ludifex examples\" },\n";
    json += "  \"scene\": 0,\n";
    json += "  \"scenes\": [ { \"nodes\": [ 0, 1 ] } ],\n";
    json += "  \"nodes\": [\n";
    json += "    { \"name\": \"face\", \"mesh\": 0, \"translation\": [-1.0, 0, 0], \"weights\": [0.25, 0.0] },\n";
    json += "    { \"name\": \"flag\", \"mesh\": 1, \"translation\": [1.0, 0, 0] }\n";
    json += "  ],\n";

    // Positions (accessor 0) must carry bounds; morph positions must too.
    json += "  \"meshes\": [\n";
    json += "    { \"name\": \"face\", \"primitives\": [ { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1 }, "
            "\"indices\": 2, \"targets\": [ { \"POSITION\": 3, \"NORMAL\": 4 }, { \"POSITION\": 5 } ] } ], "
            "\"weights\": [0.0, 0.0], \"extras\": { \"targetNames\": [\"Bulge\", \"Stretch\"] } },\n";
    json += "    { \"name\": \"flag\", \"primitives\": [ { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1 }, "
            "\"indices\": 2, \"targets\": [ { \"POSITION\": 6, \"NORMAL\": 7 } ] } ], "
            "\"extras\": { \"targetNames\": [\"Wave\"] } }\n";
    json += "  ],\n";

    json += "  \"buffers\": [ { \"byteLength\": " + std::to_string(buffer.size()) +
            ", \"uri\": \"data:application/octet-stream;base64," + EncodeBase64(buffer) + "\" } ],\n";

    json += "  \"bufferViews\": [\n";
    json += View(positionOffset, positions.size() * 4) + ",\n";
    json += View(normalOffset, normals.size() * 4) + ",\n";
    json += View(indexOffset, indices.size() * 2) + ",\n";
    json += View(bulgeOffset, bulge.size() * 4) + ",\n";
    json += View(bulgeNormalOffset, bulgeNormals.size() * 4) + ",\n";
    json += View(stretchOffset, stretch.size() * 4) + ",\n";
    json += View(waveOffset, wave.size() * 4) + ",\n";
    json += View(waveNormalOffset, waveNormals.size() * 4) + ",\n";
    json += View(timeOffset, times.size() * 4) + ",\n";
    json += View(weightOffset, weights.size() * 4) + "\n";
    json += "  ],\n";

    json += "  \"accessors\": [\n";
    json += Vec3Accessor(0, true, positions) + ",\n";
    json += Vec3Accessor(1, false, normals) + ",\n";
    json += "    { \"bufferView\": 2, \"componentType\": 5123, \"count\": " + std::to_string(indices.size()) +
            ", \"type\": \"SCALAR\" },\n";
    json += Vec3Accessor(3, true, bulge) + ",\n";
    json += Vec3Accessor(4, false, bulgeNormals) + ",\n";
    json += Vec3Accessor(5, true, stretch) + ",\n";
    json += Vec3Accessor(6, true, wave) + ",\n";
    json += Vec3Accessor(7, false, waveNormals) + ",\n";
    json += "    { \"bufferView\": 8, \"componentType\": 5126, \"count\": 3, \"type\": \"SCALAR\", "
            "\"min\": [0.0], \"max\": [2.0] },\n";
    json += "    { \"bufferView\": 9, \"componentType\": 5126, \"count\": 6, \"type\": \"SCALAR\" }\n";
    json += "  ],\n";

    json += "  \"animations\": [ { \"name\": \"breathe\", "
            "\"samplers\": [ { \"input\": 8, \"output\": 9, \"interpolation\": \"LINEAR\" } ], "
            "\"channels\": [ { \"sampler\": 0, \"target\": { \"node\": 0, \"path\": \"weights\" } } ] } ]\n";
    json += "}\n";

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    file << json;
    return file.good();
}

} // namespace examples
