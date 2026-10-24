#include "image/png.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::image {

namespace {

using Bytes = std::vector<std::uint8_t>;

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> result{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            result[n] = c;
        }
        return result;
    }();
    return table;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        c = crcTable()[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const Bytes& data) {
    constexpr std::uint32_t modulus = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t byte : data) {
        a = (a + byte) % modulus;
        b = (b + a) % modulus;
    }
    return (b << 16) | a;
}

void appendBigEndian(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void appendChunk(Bytes& out, const char (&type)[5], const Bytes& data) {
    appendBigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    appendBigEndian(out, crc32(out.data() + crcStart, out.size() - crcStart));
}

// Wraps raw bytes in a zlib stream made of uncompressed (stored) deflate blocks.
Bytes zlibStored(const Bytes& raw) {
    constexpr std::size_t maxBlock = 65535;
    Bytes out = {0x78, 0x01}; // deflate, 32K window, no preset dictionary; header check passes
    std::size_t offset = 0;
    do {
        const std::size_t length = std::min(maxBlock, raw.size() - offset);
        const bool last = offset + length == raw.size();
        out.push_back(last ? 1 : 0); // BFINAL bit, BTYPE 00 (stored)
        out.push_back(static_cast<std::uint8_t>(length));
        out.push_back(static_cast<std::uint8_t>(length >> 8));
        out.push_back(static_cast<std::uint8_t>(~length));
        out.push_back(static_cast<std::uint8_t>(~length >> 8));
        out.insert(out.end(), raw.begin() + offset, raw.begin() + offset + length);
        offset += length;
    } while (offset < raw.size());
    appendBigEndian(out, adler32(raw));
    return out;
}

} // namespace

void writePng(const std::filesystem::path& path, int width, int height, const std::uint8_t* rgb) {
    if (width <= 0 || height <= 0 || rgb == nullptr)
        throw std::runtime_error("writePng: invalid image for " + path.string());

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
    Bytes scanlines;
    scanlines.reserve((rowBytes + 1) * height);
    for (int y = 0; y < height; ++y) {
        scanlines.push_back(0); // filter type 0 (none)
        const std::uint8_t* row = rgb + rowBytes * y;
        scanlines.insert(scanlines.end(), row, row + rowBytes);
    }

    Bytes header;
    appendBigEndian(header, static_cast<std::uint32_t>(width));
    appendBigEndian(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit, truecolour, deflate, adaptive filter, no interlace

    Bytes png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    appendChunk(png, "IHDR", header);
    appendChunk(png, "IDAT", zlibStored(scanlines));
    appendChunk(png, "IEND", {});

    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file)
        throw std::runtime_error("writePng: cannot write " + path.string());
}

} // namespace evo::image
