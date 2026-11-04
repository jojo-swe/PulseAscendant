#include "audio/wav.hpp"

#include "gfx/paths.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace evo::audio {

namespace {

constexpr std::uintmax_t kMaxFileSize = 512ull * 1024 * 1024;
constexpr std::size_t kChunkHeaderSize = 8; // 4-byte tag + little-endian 32-bit size

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& problem) {
    throw std::runtime_error(gfx::pathToUtf8(path) + ": " + problem);
}

std::uint16_t readU16(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t readU32(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(readU16(bytes, offset)) |
           (static_cast<std::uint32_t>(readU16(bytes, offset + 2)) << 16);
}

bool hasTag(const Bytes& bytes, std::size_t offset, const char (&tag)[5]) {
    return std::memcmp(bytes.data() + offset, tag, 4) == 0;
}

Bytes readFile(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
        fail(path, "cannot read file (" + error.message() + ")");
    if (size > kMaxFileSize)
        fail(path, "file is larger than 512 MiB");

    std::ifstream file(path, std::ios::binary);
    Bytes bytes(static_cast<std::size_t>(size));
    if (!file || !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        fail(path, "cannot read file");
    return bytes;
}

// Throws unless the "fmt " chunk body at offset describes stereo 48 kHz 16-bit integer PCM.
void checkFormat(const std::filesystem::path& path, const Bytes& bytes, std::size_t offset,
                 std::uint32_t size) {
    if (size < 16)
        fail(path, "\"fmt \" chunk is too short");
    const std::uint16_t format = readU16(bytes, offset);
    const std::uint16_t channels = readU16(bytes, offset + 2);
    const std::uint32_t sampleRate = readU32(bytes, offset + 4);
    const std::uint16_t blockAlign = readU16(bytes, offset + 12);
    const std::uint16_t bitsPerSample = readU16(bytes, offset + 14);
    if (format != 1 || channels != 2 || sampleRate != kSampleRate || blockAlign != 4 || bitsPerSample != 16)
        fail(path, "music must be stereo 48000 Hz 16-bit PCM WAV, but this is format " +
                       std::to_string(format) + ", " + std::to_string(channels) + " channel(s), " +
                       std::to_string(sampleRate) + " Hz, " + std::to_string(bitsPerSample) + "-bit");
}

} // namespace

Wav readWav(const std::filesystem::path& path) {
    const Bytes bytes = readFile(path);

    // "RIFF" <size> "WAVE", then chunks up to the end of the RIFF size.
    if (bytes.size() < 12 || !hasTag(bytes, 0, "RIFF") || !hasTag(bytes, 8, "WAVE"))
        fail(path, "not a RIFF/WAVE file");
    const std::uint64_t riffEnd = std::uint64_t{readU32(bytes, 4)} + 8;
    if (riffEnd > bytes.size())
        fail(path, "file is truncated (the RIFF header claims " + std::to_string(riffEnd) + " bytes, found " +
                       std::to_string(bytes.size()) + ")");

    bool formatFound = false;
    std::size_t dataOffset = 0;
    std::uint32_t dataSize = 0;
    bool dataFound = false;
    for (std::size_t offset = 12; offset + kChunkHeaderSize <= riffEnd;) {
        const std::uint32_t size = readU32(bytes, offset + 4);
        const std::size_t body = offset + kChunkHeaderSize;
        if (size > riffEnd - body)
            fail(path, "chunk \"" + std::string(reinterpret_cast<const char*>(bytes.data() + offset), 4) +
                           "\" runs past the end of the file");

        if (hasTag(bytes, offset, "fmt ")) {
            checkFormat(path, bytes, body, size);
            formatFound = true;
        } else if (hasTag(bytes, offset, "data") && !dataFound) {
            dataOffset = body;
            dataSize = size;
            dataFound = true;
        }
        // Chunks are padded to an even size; the pad byte is not counted in the chunk size.
        offset = body + size + (size % 2);
    }

    if (!formatFound)
        fail(path, "no \"fmt \" chunk");
    if (!dataFound || dataSize == 0)
        fail(path, "no audio samples (missing or empty \"data\" chunk)");
    if (dataSize % 4 != 0)
        fail(path, "\"data\" chunk is not a whole number of stereo 16-bit frames");

    Wav wav;
    wav.stereo.resize(dataSize / 2);
    for (std::size_t i = 0; i < wav.stereo.size(); ++i) {
        const auto sample = static_cast<std::int16_t>(readU16(bytes, dataOffset + 2 * i));
        wav.stereo[i] = static_cast<float>(sample) / 32768.0f;
    }
    return wav;
}

} // namespace evo::audio
