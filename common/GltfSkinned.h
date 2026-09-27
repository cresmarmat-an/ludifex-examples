// Writes a skinned, animated glTF 2.0 file with its buffer embedded.
//
// A tapering column of segments bound to a chain of joints, with two clips that
// move it in clearly different ways: one sways it from side to side and loops,
// the other curls it forward and stops. The clips need to differ so that a
// blend between them is visible.

#pragma once

#include "Gltf.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace examples
{
namespace detail
{

inline void AppendFloats(std::vector<uint8_t>& buffer, const std::vector<float>& values)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
    buffer.insert(buffer.end(), bytes, bytes + values.size() * sizeof(float));
}

inline void PadTo4(std::vector<uint8_t>& buffer)
{
    while (buffer.size() % 4 != 0)
    {
        buffer.push_back(0);
    }
}

inline std::string Floats(const std::vector<float>& values)
{
    std::string text = "[";
    for (size_t index = 0; index < values.size(); ++index)
    {
        text += (index == 0 ? "" : ",") + std::to_string(values[index]);
    }
    return text + "]";
}

} // namespace detail

// jointCount joints stacked along Y, each segment `segment` metres tall, with
// two rings of vertices a segment so the skin bends rather than creasing.
//
// The clips are named "sway" and "curl".
//
// With secondSkin, a second column stands beside the first, bound to the same
// joints through a skin of its own that lists them in the opposite order. Its
// vertices index that skin, so a loader that skinned it through the first
// skin's list would bend it top for bottom; one that reads each mesh's own
// skin bends the two columns together.
inline bool WriteGltfSkinnedColumn(const std::string& path, int jointCount = 5, float segment = 0.4f,
                                   float radius = 0.16f, bool secondSkin = false)
{
    jointCount = jointCount < 2 ? 2 : (jointCount > 32 ? 32 : jointCount);

    constexpr int Around = 12;              // vertices round the column
    const int ringsPerSegment = 2;
    const int rings = (jointCount - 1) * ringsPerSegment + 1;
    const float ringHeight = segment / static_cast<float>(ringsPerSegment);
    const float totalHeight = segment * static_cast<float>(jointCount - 1);

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> weights;
    std::vector<uint8_t> joints;
    std::vector<uint16_t> indices;

    const float twoPi = 6.283185307179586f;

    for (int ring = 0; ring < rings; ++ring)
    {
        const float y = static_cast<float>(ring) * ringHeight;

        // Tapered, so which end is which is obvious at a glance.
        const float taper = 1.0f - 0.55f * (y / std::max(0.0001f, totalHeight));
        const float ringRadius = radius * taper;

        // Which joints this ring sits between, and how far along.
        const float along = y / segment;
        const int lower = static_cast<int>(along);
        const int upper = lower + 1 < jointCount ? lower + 1 : jointCount - 1;
        const float upperWeight = along - static_cast<float>(lower);

        for (int step = 0; step < Around; ++step)
        {
            const float angle = twoPi * static_cast<float>(step) / static_cast<float>(Around);
            const float x = std::cos(angle);
            const float z = std::sin(angle);

            positions.push_back(x * ringRadius);
            positions.push_back(y);
            positions.push_back(z * ringRadius);

            normals.push_back(x);
            normals.push_back(0.0f);
            normals.push_back(z);

            joints.push_back(static_cast<uint8_t>(lower));
            joints.push_back(static_cast<uint8_t>(upper));
            joints.push_back(0);
            joints.push_back(0);

            weights.push_back(1.0f - upperWeight);
            weights.push_back(upperWeight);
            weights.push_back(0.0f);
            weights.push_back(0.0f);
        }
    }

    // Caps, so the column is closed and does not show its inside from above.
    auto AddCap = [&](float y, int boneIndex, float normalY) {
        const uint16_t centre = static_cast<uint16_t>(positions.size() / 3);
        positions.push_back(0.0f);
        positions.push_back(y);
        positions.push_back(0.0f);
        normals.push_back(0.0f);
        normals.push_back(normalY);
        normals.push_back(0.0f);
        joints.push_back(static_cast<uint8_t>(boneIndex));
        joints.push_back(static_cast<uint8_t>(boneIndex));
        joints.push_back(0);
        joints.push_back(0);
        weights.push_back(1.0f);
        weights.push_back(0.0f);
        weights.push_back(0.0f);
        weights.push_back(0.0f);
        return centre;
    };

    for (int ring = 0; ring + 1 < rings; ++ring)
    {
        for (int step = 0; step < Around; ++step)
        {
            const uint16_t a = static_cast<uint16_t>(ring * Around + step);
            const uint16_t b = static_cast<uint16_t>(ring * Around + (step + 1) % Around);
            const uint16_t c = static_cast<uint16_t>((ring + 1) * Around + step);
            const uint16_t d = static_cast<uint16_t>((ring + 1) * Around + (step + 1) % Around);

            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }

    const uint16_t bottomCentre = AddCap(0.0f, 0, -1.0f);
    for (int step = 0; step < Around; ++step)
    {
        indices.push_back(bottomCentre);
        indices.push_back(static_cast<uint16_t>((step + 1) % Around));
        indices.push_back(static_cast<uint16_t>(step));
    }

    const uint16_t topCentre = AddCap(totalHeight, jointCount - 1, 1.0f);
    const int topRing = (rings - 1) * Around;
    for (int step = 0; step < Around; ++step)
    {
        indices.push_back(topCentre);
        indices.push_back(static_cast<uint16_t>(topRing + step));
        indices.push_back(static_cast<uint16_t>(topRing + (step + 1) % Around));
    }

    const size_t vertexCount = positions.size() / 3;

    // --- the skeleton --------------------------------------------------------
    //
    // Joint j sits `segment` above joint j-1 in the bind pose, so its inverse
    // bind matrix is a translation of -j * segment down Y. glTF stores
    // matrices column by column, which puts translation at elements 12 to 14.

    std::vector<float> inverseBind;
    for (int joint = 0; joint < jointCount; ++joint)
    {
        float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        m[13] = -segment * static_cast<float>(joint);
        inverseBind.insert(inverseBind.end(), m, m + 16);
    }

    // --- the clips -----------------------------------------------------------

    constexpr int SwayKeys = 5;
    std::vector<float> swayTimes;
    for (int key = 0; key < SwayKeys; ++key)
    {
        swayTimes.push_back(static_cast<float>(key) * 0.25f); // one second, looping
    }

    // Rotation about Z, growing up the chain, so the column sways like a reed.
    std::vector<float> swayRotations; // jointCount blocks of SwayKeys quaternions
    for (int joint = 0; joint < jointCount; ++joint)
    {
        // Small per joint, because a chain compounds: seven joints at a
        // tenth of a radian each is a column folded in half, not a sway.
        const float amplitude = 0.05f + 0.03f * static_cast<float>(joint);
        for (int key = 0; key < SwayKeys; ++key)
        {
            const float phase = twoPi * static_cast<float>(key) / static_cast<float>(SwayKeys - 1);
            const float angle = std::sin(phase) * amplitude;
            swayRotations.push_back(0.0f);
            swayRotations.push_back(0.0f);
            swayRotations.push_back(std::sin(angle * 0.5f));
            swayRotations.push_back(std::cos(angle * 0.5f));
        }
    }

    constexpr int CurlKeys = 2;
    const std::vector<float> curlTimes = { 0.0f, 1.0f };

    // Rotation about X, from nothing to a firm bend, and it does not loop.
    std::vector<float> curlRotations;
    for (int joint = 0; joint < jointCount; ++joint)
    {
        const float target = 0.38f;
        for (int key = 0; key < CurlKeys; ++key)
        {
            const float angle = key == 0 ? 0.0f : target;
            curlRotations.push_back(std::sin(angle * 0.5f));
            curlRotations.push_back(0.0f);
            curlRotations.push_back(0.0f);
            curlRotations.push_back(std::cos(angle * 0.5f));
        }
    }

    // The second column: beside the first, its joint indices counted from the
    // other end, and a skin whose list runs the other way to match.
    constexpr float SecondOffset = 0.6f;
    std::vector<float> secondPositions = positions;
    std::vector<uint8_t> secondJoints = joints;
    std::vector<float> secondInverseBind;
    if (secondSkin)
    {
        for (size_t index = 0; index < secondPositions.size(); index += 3)
        {
            secondPositions[index] += SecondOffset;
        }
        for (uint8_t& joint : secondJoints)
        {
            joint = static_cast<uint8_t>(jointCount - 1 - joint);
        }

        // Bound where the column stands, beside its bones, so at rest it
        // stays there.
        for (int position = 0; position < jointCount; ++position)
        {
            const int joint = jointCount - 1 - position;
            float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            m[13] = -segment * static_cast<float>(joint);
            secondInverseBind.insert(secondInverseBind.end(), m, m + 16);
        }
    }

    // --- one buffer ----------------------------------------------------------

    std::vector<uint8_t> buffer;

    const size_t positionOffset = buffer.size();
    detail::AppendFloats(buffer, positions);
    detail::PadTo4(buffer);

    const size_t normalOffset = buffer.size();
    detail::AppendFloats(buffer, normals);
    detail::PadTo4(buffer);

    const size_t jointOffset = buffer.size();
    buffer.insert(buffer.end(), joints.begin(), joints.end());
    detail::PadTo4(buffer);

    const size_t weightOffset = buffer.size();
    detail::AppendFloats(buffer, weights);
    detail::PadTo4(buffer);

    const size_t indexOffset = buffer.size();
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(indices.data());
        buffer.insert(buffer.end(), bytes, bytes + indices.size() * sizeof(uint16_t));
    }
    detail::PadTo4(buffer);

    const size_t inverseBindOffset = buffer.size();
    detail::AppendFloats(buffer, inverseBind);
    detail::PadTo4(buffer);

    const size_t swayTimeOffset = buffer.size();
    detail::AppendFloats(buffer, swayTimes);
    detail::PadTo4(buffer);

    const size_t swayRotationOffset = buffer.size();
    detail::AppendFloats(buffer, swayRotations);
    detail::PadTo4(buffer);

    const size_t curlTimeOffset = buffer.size();
    detail::AppendFloats(buffer, curlTimes);
    detail::PadTo4(buffer);

    const size_t curlRotationOffset = buffer.size();
    detail::AppendFloats(buffer, curlRotations);
    detail::PadTo4(buffer);

    const size_t secondPositionOffset = buffer.size();
    const size_t secondJointOffset = secondPositionOffset + secondPositions.size() * 4;
    const size_t secondInverseBindOffset = secondJointOffset + ((secondJoints.size() + 3) / 4) * 4;
    if (secondSkin)
    {
        detail::AppendFloats(buffer, secondPositions);
        detail::PadTo4(buffer);
        buffer.insert(buffer.end(), secondJoints.begin(), secondJoints.end());
        detail::PadTo4(buffer);
        detail::AppendFloats(buffer, secondInverseBind);
        detail::PadTo4(buffer);
    }

    // --- the JSON ------------------------------------------------------------

    float minimum[3] = { positions[0], positions[1], positions[2] };
    float maximum[3] = { positions[0], positions[1], positions[2] };
    for (size_t index = 0; index < vertexCount; ++index)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], positions[index * 3 + static_cast<size_t>(axis)]);
            maximum[axis] = std::max(maximum[axis], positions[index * 3 + static_cast<size_t>(axis)]);
        }
    }

    std::string json;
    json += "{\n";
    json += "  \"asset\": { \"version\": \"2.0\", \"generator\": \"ludifex examples\" },\n";
    json += "  \"scene\": 0,\n";
    const int secondNode = jointCount + 1;
    json += secondSkin ? "  \"scenes\": [ { \"nodes\": [ 0, 1, " + std::to_string(secondNode) + " ] } ],\n"
                       : std::string("  \"scenes\": [ { \"nodes\": [ 0, 1 ] } ],\n");

    // Node 0 draws the mesh and names the skin; nodes 1 onward are the joints,
    // each a child of the one below it.
    json += "  \"nodes\": [\n";
    json += "    { \"mesh\": 0, \"skin\": 0, \"name\": \"column\" }";
    for (int joint = 0; joint < jointCount; ++joint)
    {
        json += ",\n    { \"name\": \"joint" + std::to_string(joint) + "\"";
        json += ", \"translation\": [0," + std::to_string(joint == 0 ? 0.0f : segment) + ",0]";
        if (joint + 1 < jointCount)
        {
            json += ", \"children\": [" + std::to_string(joint + 2) + "]";
        }
        json += " }";
    }
    if (secondSkin)
    {
        json += ",\n    { \"mesh\": 1, \"skin\": 1, \"name\": \"second column\" }";
    }
    json += "\n  ],\n";

    std::string jointList;
    for (int joint = 0; joint < jointCount; ++joint)
    {
        jointList += (joint == 0 ? "" : ",") + std::to_string(joint + 1);
    }
    // Accessors for the second column come after every other accessor: six
    // for the geometry and binding, then each clip's times and its rotations.
    const int secondFirstAccessor = 6 + (1 + jointCount) * 2;

    std::string reversedList;
    for (int joint = jointCount - 1; joint >= 0; --joint)
    {
        reversedList += (joint == jointCount - 1 ? "" : ",") + std::to_string(joint + 1);
    }

    json += "  \"skins\": [ { \"inverseBindMatrices\": 5, \"joints\": [" + jointList + "], \"skeleton\": 1 }";
    if (secondSkin)
    {
        json += ", { \"inverseBindMatrices\": " + std::to_string(secondFirstAccessor + 2) + ", \"joints\": [" +
                reversedList + "], \"skeleton\": 1 }";
    }
    json += " ],\n";

    json += "  \"meshes\": [ { \"primitives\": [ { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, "
            "\"JOINTS_0\": 2, \"WEIGHTS_0\": 3 }, \"indices\": 4 } ] }";
    if (secondSkin)
    {
        json += ", { \"primitives\": [ { \"attributes\": { \"POSITION\": " + std::to_string(secondFirstAccessor) +
                ", \"NORMAL\": 1, \"JOINTS_0\": " + std::to_string(secondFirstAccessor + 1) +
                ", \"WEIGHTS_0\": 3 }, \"indices\": 4 } ] }";
    }
    json += " ],\n";

    json += "  \"buffers\": [ { \"byteLength\": " + std::to_string(buffer.size()) +
            ", \"uri\": \"data:application/octet-stream;base64," + EncodeBase64(buffer) + "\" } ],\n";

    auto View = [](size_t offset, size_t bytes, const char* target) {
        std::string text = "    { \"buffer\": 0, \"byteOffset\": " + std::to_string(offset) +
                           ", \"byteLength\": " + std::to_string(bytes);
        if (target != nullptr)
        {
            text += ", \"target\": ";
            text += target;
        }
        return text + " }";
    };

    json += "  \"bufferViews\": [\n";
    json += View(positionOffset, positions.size() * 4, "34962") + ",\n";
    json += View(normalOffset, normals.size() * 4, "34962") + ",\n";
    json += View(jointOffset, joints.size(), "34962") + ",\n";
    json += View(weightOffset, weights.size() * 4, "34962") + ",\n";
    json += View(indexOffset, indices.size() * 2, "34963") + ",\n";
    json += View(inverseBindOffset, inverseBind.size() * 4, nullptr) + ",\n";
    json += View(swayTimeOffset, swayTimes.size() * 4, nullptr) + ",\n";
    json += View(swayRotationOffset, swayRotations.size() * 4, nullptr) + ",\n";
    json += View(curlTimeOffset, curlTimes.size() * 4, nullptr) + ",\n";
    json += View(curlRotationOffset, curlRotations.size() * 4, nullptr);
    if (secondSkin)
    {
        json += ",\n" + View(secondPositionOffset, secondPositions.size() * 4, "34962") + ",\n";
        json += View(secondJointOffset, secondJoints.size(), "34962") + ",\n";
        json += View(secondInverseBindOffset, secondInverseBind.size() * 4, nullptr);
    }
    json += "\n  ],\n";

    json += "  \"accessors\": [\n";
    json += "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": " + std::to_string(vertexCount) +
            ", \"type\": \"VEC3\", \"min\": " +
            detail::Floats({ minimum[0], minimum[1], minimum[2] }) +
            ", \"max\": " + detail::Floats({ maximum[0], maximum[1], maximum[2] }) + " },\n";
    json += "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": " + std::to_string(vertexCount) +
            ", \"type\": \"VEC3\" },\n";
    json += "    { \"bufferView\": 2, \"componentType\": 5121, \"count\": " + std::to_string(vertexCount) +
            ", \"type\": \"VEC4\" },\n";
    json += "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": " + std::to_string(vertexCount) +
            ", \"type\": \"VEC4\" },\n";
    json += "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": " +
            std::to_string(indices.size()) + ", \"type\": \"SCALAR\" },\n";
    json += "    { \"bufferView\": 5, \"componentType\": 5126, \"count\": " + std::to_string(jointCount) +
            ", \"type\": \"MAT4\" },\n";

    // Animation inputs carry min and max, which the specification requires and
    // which a reader is entitled to trust.
    json += "    { \"bufferView\": 6, \"componentType\": 5126, \"count\": " + std::to_string(SwayKeys) +
            ", \"type\": \"SCALAR\", \"min\": [0.0], \"max\": [" + std::to_string(swayTimes.back()) +
            "] },\n";

    for (int joint = 0; joint < jointCount; ++joint)
    {
        json += "    { \"bufferView\": 7, \"byteOffset\": " +
                std::to_string(static_cast<size_t>(joint) * SwayKeys * 4 * 4) +
                ", \"componentType\": 5126, \"count\": " + std::to_string(SwayKeys) +
                ", \"type\": \"VEC4\" },\n";
    }

    json += "    { \"bufferView\": 8, \"componentType\": 5126, \"count\": " + std::to_string(CurlKeys) +
            ", \"type\": \"SCALAR\", \"min\": [0.0], \"max\": [" + std::to_string(curlTimes.back()) +
            "] },\n";

    for (int joint = 0; joint < jointCount; ++joint)
    {
        json += "    { \"bufferView\": 9, \"byteOffset\": " +
                std::to_string(static_cast<size_t>(joint) * CurlKeys * 4 * 4) +
                ", \"componentType\": 5126, \"count\": " + std::to_string(CurlKeys) +
                ", \"type\": \"VEC4\" }";
        json += (joint + 1 < jointCount || secondSkin) ? ",\n" : "\n";
    }
    if (secondSkin)
    {
        json += "    { \"bufferView\": 10, \"componentType\": 5126, \"count\": " + std::to_string(vertexCount) +
                ", \"type\": \"VEC3\", \"min\": " +
                detail::Floats({ minimum[0] + SecondOffset, minimum[1], minimum[2] }) +
                ", \"max\": " + detail::Floats({ maximum[0] + SecondOffset, maximum[1], maximum[2] }) + " },\n";
        json += "    { \"bufferView\": 11, \"componentType\": 5121, \"count\": " + std::to_string(vertexCount) +
                ", \"type\": \"VEC4\" },\n";
        json += "    { \"bufferView\": 12, \"componentType\": 5126, \"count\": " + std::to_string(jointCount) +
                ", \"type\": \"MAT4\" }\n";
    }
    json += "  ],\n";

    // Accessor numbering: 0-5 geometry and bind, 6 sway times, 7.. sway
    // rotations, then curl times and rotations after them.
    const int swayTimeAccessor = 6;
    const int swayFirstRotation = 7;
    const int curlTimeAccessor = swayFirstRotation + jointCount;
    const int curlFirstRotation = curlTimeAccessor + 1;

    auto Clip = [&](const char* name, int timeAccessor, int firstRotation) {
        std::string text = "    { \"name\": \"";
        text += name;
        text += "\", \"samplers\": [\n";
        for (int joint = 0; joint < jointCount; ++joint)
        {
            text += "      { \"input\": " + std::to_string(timeAccessor) +
                    ", \"output\": " + std::to_string(firstRotation + joint) +
                    ", \"interpolation\": \"LINEAR\" }";
            text += (joint + 1 < jointCount) ? ",\n" : "\n";
        }
        text += "    ], \"channels\": [\n";
        for (int joint = 0; joint < jointCount; ++joint)
        {
            text += "      { \"sampler\": " + std::to_string(joint) + ", \"target\": { \"node\": " +
                    std::to_string(joint + 1) + ", \"path\": \"rotation\" } }";
            text += (joint + 1 < jointCount) ? ",\n" : "\n";
        }
        text += "    ] }";
        return text;
    };

    json += "  \"animations\": [\n";
    json += Clip("sway", swayTimeAccessor, swayFirstRotation) + ",\n";
    json += Clip("curl", curlTimeAccessor, curlFirstRotation) + "\n";
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

} // namespace examples
