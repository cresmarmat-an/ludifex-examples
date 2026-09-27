// Writes a short decaying tone as a 16-bit mono WAV file.
//
// The examples generate their own sounds, so they need no audio files.

#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace examples
{

inline bool WriteToneWav(const std::string& path, float frequency, int milliseconds,
                         float amplitude, float decayRate = 16.0f)
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr int SampleRate = 44100;

    const int sampleCount = SampleRate * milliseconds / 1000;
    if (sampleCount <= 0)
    {
        return false;
    }

    std::vector<int16_t> samples(static_cast<size_t>(sampleCount));
    for (int index = 0; index < sampleCount; ++index)
    {
        const float time = static_cast<float>(index) / static_cast<float>(SampleRate);
        const float envelope = std::exp(-time * decayRate);
        const float value = std::sin(time * frequency * 2.0f * Pi) * envelope * amplitude;
        samples[static_cast<size_t>(index)] = static_cast<int16_t>(value * 32000.0f);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));

    auto WriteU32 = [&](uint32_t value) { file.write(reinterpret_cast<const char*>(&value), 4); };
    auto WriteU16 = [&](uint16_t value) { file.write(reinterpret_cast<const char*>(&value), 2); };

    file.write("RIFF", 4);
    WriteU32(36 + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    WriteU32(16);
    WriteU16(1);                             // PCM
    WriteU16(1);                             // mono
    WriteU32(SampleRate);
    WriteU32(SampleRate * 2);                // byte rate
    WriteU16(2);                             // block align
    WriteU16(16);                            // bits per sample
    file.write("data", 4);
    WriteU32(dataBytes);
    file.write(reinterpret_cast<const char*>(samples.data()), dataBytes);

    return file.good();
}

} // namespace examples
