#include "audio/transport.hpp"
#include "audio/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kFrames = 48000;

void appendU16(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendU32(Bytes& out, std::uint32_t value) {
    appendU16(out, value & 0xFFFF);
    appendU16(out, value >> 16);
}

void appendTag(Bytes& out, const char (&tag)[5]) {
    out.insert(out.end(), tag, tag + 4);
}

// The left channel of frame i holds i % 30000 and the right channel its negation, so a rendered sample
// tells which frame it came from.
std::int16_t leftSample(std::uint32_t frame) {
    return static_cast<std::int16_t>(frame % 30000);
}

// Writes a stereo PCM16 WAV of kFrames frames. A 3-byte "LIST" chunk (odd size, so followed by one pad
// byte) sits between "fmt " and "data" to check that the reader skips unknown chunks correctly.
void writeWav(const std::filesystem::path& path, std::uint32_t sampleRate) {
    Bytes body;
    appendTag(body, "WAVE");

    appendTag(body, "fmt ");
    appendU32(body, 16);
    appendU16(body, 1); // PCM
    appendU16(body, 2); // channels
    appendU32(body, sampleRate);
    appendU32(body, sampleRate * 4); // byte rate
    appendU16(body, 4);              // block align
    appendU16(body, 16);             // bits per sample

    appendTag(body, "LIST");
    appendU32(body, 3);
    body.insert(body.end(), {'a', 'b', 'c', 0});

    appendTag(body, "data");
    appendU32(body, kFrames * 4);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const std::int16_t left = leftSample(frame);
        appendU16(body, static_cast<std::uint16_t>(left));
        appendU16(body, static_cast<std::uint16_t>(-left));
    }

    Bytes file;
    appendTag(file, "RIFF");
    appendU32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out)
        throw std::runtime_error("could not write " + path.string());
}

float expectedLeft(std::uint32_t frame) {
    return static_cast<float>(leftSample(frame)) / 32768.0f;
}

// Returns the exception message, or an empty string when readWav did not throw.
std::string readError(const std::filesystem::path& path) {
    try {
        evo::audio::readWav(path);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

} // namespace

int main() {
    try {
        const std::filesystem::path dir = std::filesystem::path(EVO_TEST_OUT_DIR) / "audio";
        const std::filesystem::path wavPath = dir / "one_second_48k.wav";
        writeWav(wavPath, 48000);

        evo::audio::Wav wav = evo::audio::readWav(wavPath);
        CHECK(wav.frames() == kFrames);
        CHECK(wav.stereo.size() == kFrames * 2);
        CHECK(wav.stereo[2 * 1234] == expectedLeft(1234));
        CHECK(wav.stereo[2 * 1234 + 1] == -expectedLeft(1234));

        evo::audio::Transport transport(std::move(wav));
        CHECK(transport.durationSeconds() == 1.0);
        CHECK(transport.frame() == 0);

        transport.seek(0.5);
        CHECK(transport.frame() == 24000);
        CHECK(transport.positionSeconds() == 0.5);

        transport.seek(1.5); // wraps past the end
        CHECK(transport.frame() == 24000);

        transport.seek(1.0); // exactly the duration is the start again
        CHECK(transport.frame() == 0);

        transport.seek(-1.0);
        CHECK(transport.frame() == 0);

        transport.seek(0.25);
        transport.seek(std::numeric_limits<double>::quiet_NaN());
        CHECK(transport.frame() == 0);

        // 0.99999 s is frame 47999.52, so the render starts on the last frame and wraps to frame 0.
        transport.seek(0.99999);
        CHECK(transport.frame() == kFrames - 1);
        std::vector<float> out(10 * 2, 99.0f);
        transport.render(out.data(), 10);
        CHECK(out[0] == expectedLeft(kFrames - 1));
        CHECK(out[1] == -expectedLeft(kFrames - 1));
        for (std::uint32_t i = 1; i < 10; ++i) {
            CHECK(out[2 * i] == expectedLeft(i - 1));
            CHECK(out[2 * i + 1] == -expectedLeft(i - 1));
        }
        CHECK(transport.frame() == 9);

        // Muted: zeros, but the position still advances.
        transport.setMuted(true);
        std::fill(out.begin(), out.end(), 99.0f);
        transport.render(out.data(), 10);
        for (float sample : out)
            CHECK(sample == 0.0f);
        CHECK(transport.frame() == 19);

        transport.setMuted(false);
        transport.render(out.data(), 1);
        CHECK(out[0] == expectedLeft(19));

        const std::filesystem::path wrongRatePath = dir / "one_second_44k1.wav";
        writeWav(wrongRatePath, 44100);
        const std::string wrongRate = readError(wrongRatePath);
        CHECK(wrongRate.find("48000") != std::string::npos);
        CHECK(wrongRate.find(wrongRatePath.string()) != std::string::npos);

        const std::filesystem::path missingPath = dir / "missing.wav";
        std::filesystem::remove(missingPath);
        const std::string missing = readError(missingPath);
        CHECK(missing.find(missingPath.string()) != std::string::npos);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
