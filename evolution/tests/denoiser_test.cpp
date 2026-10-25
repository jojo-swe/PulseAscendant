// The denoiser on synthetic G-buffers uploaded from the CPU: demodulate, reproject and accumulate, à-trous,
// remodulate plus the primary-hit emission. Noise converges under accumulation; a reset (or a rejected
// reprojection) starts the history over, and a first frame equals the stateless (export) path byte for byte
// (Review Focus 6); a moving pattern follows its motion vectors, fractional motion interpolates bilinearly;
// the temporal variance estimates the sample variance, also for bright pixels; à-trous keeps depth edges and
// lighting-only edges at length 1 and removes noise from flat regions; a firefly decays at the stated rate;
// non-finite guides never match; the emission never enters history or filtering (Review Focus 5); NaN, Inf
// and huge samples never reach the output or the history (Review Focus 8); the same inputs give the same
// bytes. Frames run with two in flight, with the validation layer and its synchronization validation on;
// fails on any message.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "post/denoiser.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace evo;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

template <typename Exception, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const Exception& error) {
        std::printf("  refused: %s\n", error.what());
        return true;
    }
    return false;
}

constexpr std::uint32_t kSize = 64;

// --- Half floats (the guides and outputs are RGBA16F)
// ---------------------------------------------------------

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float magnitude;
    if (exponent == 0)
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        magnitude =
            mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -magnitude : magnitude;
}

// Round to nearest even; the test only feeds finite values well inside the half range, plus NaN.
std::uint16_t floatToHalf(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    if (std::isnan(f))
        return static_cast<std::uint16_t>(sign | 0x7E00u);
    const float magnitude = std::fabs(f);
    if (magnitude >= 65520.0f)
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (magnitude < std::ldexp(1.0f, -14)) { // subnormal half: multiples of 2^-24
        const auto m = static_cast<std::uint32_t>(std::nearbyint(magnitude * 16777216.0f));
        return static_cast<std::uint16_t>(sign | m);
    }
    int exponent;
    std::frexp(magnitude, &exponent);                          // magnitude = m * 2^exponent, m in [0.5, 1)
    const float scaled = std::ldexp(magnitude, 11 - exponent); // in [1024, 2048)
    auto mantissa = static_cast<std::uint32_t>(std::nearbyint(scaled));
    int halfExponent = exponent - 1 + 15;
    if (mantissa == 2048) {
        mantissa = 1024;
        ++halfExponent;
    }
    if (halfExponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    return static_cast<std::uint16_t>(sign | (halfExponent << 10) | (mantissa & 0x3FFu));
}

// Whether a positive half is `value` converted to fp16 as Vulkan allows (round to nearest or towards zero):
// equal, or one of the two halves around it.
bool isHalfOf(std::uint16_t bits, float value) {
    const float h = halfToFloat(bits);
    if (h == value)
        return true;
    return h < value ? halfToFloat(static_cast<std::uint16_t>(bits + 1)) > value
                     : halfToFloat(static_cast<std::uint16_t>(bits - 1)) < value;
}

// --- Synthetic G-buffers
// --------------------------------------------------------------------------------------

// One frame's path tracer outputs on the CPU, in the denoiser's input formats.
struct Frame {
    std::uint32_t width = 0, height = 0;
    std::vector<float> lighting, emission, albedo, normal, motion, volume; // RGBA per pixel
    std::vector<float> depth;                                              // one per pixel

    float* at(std::vector<float>& image, std::uint32_t x, std::uint32_t y) {
        return &image[(std::size_t{y} * width + x) * 4];
    }
};

// Black lighting and emission, white albedo, a plane facing the camera at depth 1, no motion, no medium
// (volume = (0, 0, 0, 1)).
Frame flatFrame(std::uint32_t width = kSize, std::uint32_t height = kSize) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    const std::size_t pixels = std::size_t{width} * height;
    frame.lighting.assign(pixels * 4, 0.0f);
    frame.emission.assign(pixels * 4, 0.0f);
    frame.albedo.assign(pixels * 4, 1.0f);
    frame.normal.assign(pixels * 4, 0.0f);
    frame.motion.assign(pixels * 4, 0.0f);
    frame.volume.assign(pixels * 4, 0.0f);
    frame.depth.assign(pixels, 1.0f);
    for (std::size_t i = 0; i < pixels; ++i) {
        frame.normal[i * 4 + 2] = 1.0f;
        frame.normal[i * 4 + 3] = 0.5f; // roughness
        frame.motion[i * 4 + 2] = 1.0f; // valid
        frame.volume[i * 4 + 3] = 1.0f; // transmittance
    }
    return frame;
}

std::uint32_t pcg(std::uint32_t v) {
    const std::uint32_t state = v * 747796405u + 2891336453u;
    const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Uniform in [-0.5, 0.5), a pure function of its arguments.
float noise(std::uint32_t frame, std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
    const std::uint32_t h = pcg(pcg(pcg(pcg(frame) ^ x) ^ (y * 0x9E3779B9u)) ^ channel);
    return static_cast<float>(h >> 8) / 16777216.0f - 0.5f;
}

// Lighting `base` plus fresh uniform noise of the given amplitude per pixel and channel.
Frame noisyFrame(std::uint32_t index, float base, float amplitude) {
    Frame frame = flatFrame();
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                frame.at(frame.lighting, x, y)[c] = base + 2.0f * amplitude * noise(index, x, y, c);
    return frame;
}

// --- The harness: upload, denoise, read back
// -----------------------------------------------------------------

// What one frame read back: the HDR output, the illumination history, the history length image and the
// volume layer's history.
struct Output {
    std::vector<std::uint16_t> hdrBits, historyBits, lengthBits, volumeBits; // RGBA16F
    float hdr(std::uint32_t x, std::uint32_t y, std::uint32_t c) const { return value(hdrBits, x, y, c); }
    float history(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return value(historyBits, x, y, c);
    }
    float volume(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return value(volumeBits, x, y, c);
    }
    float volumeLength(std::uint32_t x, std::uint32_t y) const { return value(lengthBits, x, y, 3); }
    float length(std::uint32_t x, std::uint32_t y) const { return value(lengthBits, x, y, 0); }
    std::uint32_t width = 0;

private:
    float value(const std::vector<std::uint16_t>& bits, std::uint32_t x, std::uint32_t y,
                std::uint32_t c) const {
        return halfToFloat(bits[(std::size_t{y} * width + x) * 4 + c]);
    }
};

gfx::Image makeImage(gfx::Context& ctx, std::uint32_t width, std::uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, const char* name) {
    return gfx::Image(ctx, {width, height, format, usage, 1, name});
}

// The G-buffer images, the HDR output, per-slot staging and readback buffers (alive for the whole run, so
// synchronization validation never sees a reused allocation) and a denoiser on its own ring. Frames are
// recorded with two in flight; a slot's readback is collected when the ring reuses the slot.
class Harness {
public:
    Harness(gfx::Context& ctx, std::uint32_t width = kSize, std::uint32_t height = kSize)
        : ctx_(ctx), ring_(std::make_unique<gfx::FrameRing>(ctx)) {
        createImages(width, height);
        denoiser_ = std::make_unique<post::Denoiser>(ctx, *ring_, width, height);
    }
    ~Harness() {
        ring_->waitAll();
        denoiser_.reset();
        ring_.reset();
    }

    post::Denoiser& denoiser() { return *denoiser_; }

    void resize(std::uint32_t width, std::uint32_t height) {
        collectAll();
        ring_->waitAll();
        vkQueueWaitIdle(ctx_.queue()); // see main(): the layer credits a queue wait, not a timeline wait
        denoiser_->resize(width, height);
        createImages(width, height);
    }

    // Denoises each frame in turn and returns what each one read back.
    std::vector<Output> run(const std::vector<Frame>& frames, const post::DenoiseSettings& settings) {
        std::vector<Output> outputs(frames.size());
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const std::uint32_t slot = ring_->slot();
            collect(slot);
            VkCommandBuffer cmd = ring_->begin();
            upload(cmd, slot, frames[i]);
            post::GBufferView view{lighting_, emission_, albedo_, normal_, depth_, motion_, volume_};
            denoiser_->denoise(cmd, view, settings, hdr_);
            readback(cmd, slot);
            pending_[slot] = Pending{ring_->submit(), &outputs[i]};
        }
        collectAll();
        return outputs;
    }
    Output runOne(const Frame& frame, const post::DenoiseSettings& settings) {
        return run({frame}, settings).front();
    }

    // Records one denoise with the given images in place of the harness's own (to check refusals).
    void denoiseWith(const post::GBufferView& view, gfx::Image& hdr,
                     const post::DenoiseSettings& settings = {}) {
        VkCommandBuffer cmd = ring_->begin();
        try {
            denoiser_->denoise(cmd, view, settings, hdr);
        } catch (...) {
            ring_->submit();
            throw;
        }
        ring_->submit();
    }

    gfx::Image lighting_, emission_, albedo_, normal_, depth_, motion_, volume_, hdr_;

private:
    struct Pending {
        std::uint64_t done = 0;
        Output* output = nullptr;
    };

    std::size_t pixels() const { return std::size_t{width_} * height_; }

    void createImages(std::uint32_t width, std::uint32_t height) {
        width_ = width;
        height_ = height;
        constexpr VkImageUsageFlags in = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        lighting_ = makeImage(ctx_, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, in, "test lighting");
        emission_ = makeImage(ctx_, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, in, "test emission");
        albedo_ = makeImage(ctx_, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, in, "test albedo");
        normal_ = makeImage(ctx_, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, in, "test normal");
        depth_ = makeImage(ctx_, width, height, VK_FORMAT_R32_SFLOAT, in, "test depth");
        motion_ = makeImage(ctx_, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, in, "test motion");
        volume_ = makeImage(ctx_, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, in, "test volume");
        hdr_ = makeImage(ctx_, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "test hdr");
        const VkDeviceSize stagingBytes = pixels() * (16 + 5 * 8 + 4);
        const VkDeviceSize readbackBytes = pixels() * 8 * 4;
        for (std::size_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot) {
            // Kept, not freed: a new size gets new buffers beside the old ones.
            retired_.push_back(std::move(staging_[slot]));
            retired_.push_back(std::move(readback_[slot]));
            staging_[slot] =
                gfx::Buffer(ctx_, {stagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "staging"});
            readback_[slot] =
                gfx::Buffer(ctx_, {readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "readback"});
        }
    }

    void upload(VkCommandBuffer cmd, std::uint32_t slot, const Frame& frame) {
        if (frame.width != width_ || frame.height != height_)
            throw std::logic_error("frame size differs from the harness");
        auto* bytes = static_cast<std::uint8_t*>(staging_[slot].mapped());
        VkDeviceSize offset = 0;
        const auto copy = [&](gfx::Image& image, const void* data, VkDeviceSize size) {
            std::memcpy(bytes + offset, data, size);
            image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width_, height_, 1};
            vkCmdCopyBufferToImage(cmd, staging_[slot].handle(), image.handle(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            offset += size;
        };
        const auto halves = [](const std::vector<float>& values) {
            std::vector<std::uint16_t> out(values.size());
            for (std::size_t i = 0; i < values.size(); ++i)
                out[i] = floatToHalf(values[i]);
            return out;
        };
        copy(lighting_, frame.lighting.data(), pixels() * 16);
        const auto emission = halves(frame.emission), albedo = halves(frame.albedo),
                   normal = halves(frame.normal), motion = halves(frame.motion),
                   volume = halves(frame.volume);
        copy(emission_, emission.data(), pixels() * 8);
        copy(albedo_, albedo.data(), pixels() * 8);
        copy(normal_, normal.data(), pixels() * 8);
        copy(motion_, motion.data(), pixels() * 8);
        copy(volume_, volume.data(), pixels() * 8);
        copy(depth_, frame.depth.data(), pixels() * 4);
        staging_[slot].flush();
    }

    void readback(VkCommandBuffer cmd, std::uint32_t slot) {
        const VkBuffer buffer = readback_[slot].handle();
        // This slot's previous copy into the buffer finished before the host read it, but order the GPU
        // writes too (write after write).
        gfx::bufferBarrier2(cmd, buffer, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        gfx::Image* sources[4] = {&hdr_, &denoiser_->history(), &denoiser_->historyLength(),
                                  &denoiser_->volumeHistory()};
        for (int i = 0; i < 4; ++i) {
            sources[i]->transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                                   VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.bufferOffset = pixels() * 8 * i;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width_, height_, 1};
            vkCmdCopyImageToBuffer(cmd, sources[i]->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                                   &region);
        }
        gfx::bufferBarrier2(cmd, buffer, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    }

    void collect(std::uint32_t slot) {
        Pending& pending = pending_[slot];
        if (!pending.output)
            return;
        ring_->wait(pending.done);
        readback_[slot].invalidate();
        const auto* bits = static_cast<const std::uint16_t*>(readback_[slot].mapped());
        const std::size_t count = pixels() * 4;
        Output& out = *pending.output;
        out.width = width_;
        out.hdrBits.assign(bits, bits + count);
        out.historyBits.assign(bits + count, bits + 2 * count);
        out.lengthBits.assign(bits + 2 * count, bits + 3 * count);
        out.volumeBits.assign(bits + 3 * count, bits + 4 * count);
        pending.output = nullptr;
    }
    void collectAll() {
        for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot)
            collect(slot);
    }

    gfx::Context& ctx_;
    std::uint32_t width_ = 0, height_ = 0;
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> staging_, readback_;
    std::vector<gfx::Buffer> retired_;
    std::array<Pending, gfx::FrameRing::kFramesInFlight> pending_{};
    std::unique_ptr<gfx::FrameRing> ring_;
    std::unique_ptr<post::Denoiser> denoiser_;
};

// --- Measures
// --------------------------------------------------------------------------------------------------

double rmse(const Output& out, float expected) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            for (std::uint32_t c = 0; c < 3; ++c) {
                const double d = out.hdr(x, y, c) - expected;
                sum += d * d;
            }
    return std::sqrt(sum / (kSize * kSize * 3.0));
}

bool allLengths(const Output& out, float expected) {
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            if (out.length(x, y) != expected)
                return false;
    return true;
}

bool allFinite(const std::vector<std::uint16_t>& bits) {
    for (std::uint16_t h : bits)
        if (((h >> 10) & 0x1Fu) == 31u)
            return false;
    return true;
}

post::DenoiseSettings noFilter() {
    post::DenoiseSettings settings;
    settings.atrousIterations = 0;
    return settings;
}

// --- Tests
// -----------------------------------------------------------------------------------------------------

// (a) Fresh noise every frame converges under accumulation; the history counts the frames.
void testNoiseConverges(gfx::Context& ctx) {
    std::printf("noise converges under accumulation\n");
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 32; ++i)
        frames.push_back(noisyFrame(i, 0.5f, 0.5f));

    // The plan's defaults: alphaColor 0.2 settles at sqrt(sigma^2 * a / (2 - a)) = sqrt(1/12 / 9) = 0.096.
    Harness defaults(ctx);
    const std::vector<Output> out = defaults.run(frames, noFilter());
    for (std::uint32_t i = 0; i < 32; ++i)
        CHECK(allLengths(out[i], static_cast<float>(i + 1)));
    const double after4 = rmse(out[3], 0.5f), after32 = rmse(out[31], 0.5f);
    std::printf("  alpha 0.2: RMSE after 1 frame %.4f, 4 frames %.4f, 32 frames %.4f\n", rmse(out[0], 0.5f),
                after4, after32);
    CHECK(after32 < after4);
    CHECK(after32 < 0.11);

    // With alphaColor below 1/32 the first 32 frames are a plain mean: sqrt(1/12 / 32) = 0.051.
    post::DenoiseSettings slow = noFilter();
    slow.alphaColor = slow.alphaMoments = 0.02f;
    Harness mean(ctx);
    const std::vector<Output> meanOut = mean.run(frames, slow);
    const double mean4 = rmse(meanOut[3], 0.5f), mean32 = rmse(meanOut[31], 0.5f);
    std::printf("  alpha 0.02: RMSE after 4 frames %.4f, 32 frames %.4f\n", mean4, mean32);
    CHECK(mean32 < 0.08);
    CHECK(mean32 < mean4);
    CHECK(allLengths(meanOut[31], 32.0f));

    // maxHistory caps the length.
    post::DenoiseSettings capped = noFilter();
    capped.maxHistory = 5;
    Harness cap(ctx);
    const std::vector<Output> capOut = cap.run({frames.begin(), frames.begin() + 8}, capped);
    CHECK(allLengths(capOut[4], 5.0f));
    CHECK(allLengths(capOut[7], 5.0f));
}

// (b) After resetHistory() the next frame starts over: length 1, and the same bytes as a fresh denoiser's
// first frame and as the stateless path (temporal = false), with the default 3 à-trous iterations.
void testResetMatchesFreshAndStateless(gfx::Context& ctx) {
    std::printf("reset equals a fresh denoiser and temporal = false\n");
    std::vector<Frame> warmup;
    for (std::uint32_t i = 0; i < 5; ++i)
        warmup.push_back(noisyFrame(100 + i, 0.4f, 0.3f));
    const Frame probe = noisyFrame(200, 0.6f, 0.3f);

    Harness used(ctx);
    const auto warm = used.run(warmup, {});
    CHECK(allLengths(warm.back(), 5.0f));
    used.denoiser().resetHistory();
    const Output afterReset = used.runOne(probe, {});
    CHECK(allLengths(afterReset, 1.0f));

    Harness fresh(ctx);
    const Output first = fresh.runOne(probe, {});
    CHECK(allLengths(first, 1.0f));
    CHECK(afterReset.hdrBits == first.hdrBits);
    CHECK(afterReset.historyBits == first.historyBits);

    post::DenoiseSettings stateless;
    stateless.temporal = false;
    Harness exportPath(ctx);
    exportPath.run(warmup, stateless); // history it may keep must not matter
    const Output exported = exportPath.runOne(probe, stateless);
    CHECK(exported.hdrBits == first.hdrBits);
    CHECK(allLengths(exported, 1.0f));

    // A denoiser that accumulated, then switched to temporal = false, equals them as well.
    const Output switched = used.runOne(probe, stateless);
    CHECK(switched.hdrBits == first.hdrBits);

    // The first frame of a denoiser with 0 iterations shows the input's illumination unchanged, up to the
    // half conversion (which Vulkan lets round to nearest or towards zero: within one ulp of [0.5, 1),
    // 2^-11).
    Harness plain(ctx);
    const Output unfiltered = plain.runOne(probe, noFilter());
    Frame copy = probe;
    double worst = 0.0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                worst = std::max(worst, static_cast<double>(std::fabs(unfiltered.hdr(x, y, c) -
                                                                      copy.at(copy.lighting, x, y)[c])));
    CHECK(worst <= 1.0 / 2048.0);
}

// (c) A guide that no longer matches rejects the history: a 50 % depth change on the right half, a turned
// normal on the bottom rows, invalid motion in one column.
void testDisocclusionRejectsHistory(gfx::Context& ctx) {
    std::printf("depth, normal and motion validity reject history\n");
    Frame first = flatFrame();
    Frame second = flatFrame();
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = kSize / 2; x < kSize; ++x)
            second.depth[std::size_t{y} * kSize + x] = 1.5f;
    for (std::uint32_t y = kSize - 4; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize / 2; ++x) {
            float* n = second.at(second.normal, x, y);
            n[0] = 0.6f; // dot with (0, 0, 1) = 0.8 < 0.9
            n[2] = 0.8f;
        }
    for (std::uint32_t y = 0; y < kSize; ++y)
        second.at(second.motion, 5, y)[2] = 0.0f; // no previous position
    // A small depth change within 5 % keeps the history.
    for (std::uint32_t x = 0; x < kSize / 2; ++x)
        second.depth[x] = 1.04f;

    Harness harness(ctx);
    const auto out = harness.run({first, second}, {});
    CHECK(allLengths(out[0], 1.0f));
    bool right = true, left = true, bottom = true, column = true;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float length = out[1].length(x, y);
            if (x >= kSize / 2)
                right = right && length == 1.0f;
            else if (x == 5)
                column = column && length == 1.0f;
            else if (y >= kSize - 4)
                bottom = bottom && length == 1.0f;
            else
                left = left && length == 2.0f;
        }
    CHECK(right);
    CHECK(left);
    CHECK(bottom);
    CHECK(column);
}

// (d) A pattern moving 3 px to the right per frame, with motion -3, stays sharp: after 8 frames every pixel
// beyond a 3 px border is within 2 % of the pattern, and the history length shows how long each pixel has
// been on screen.
void testReprojectionFollowsMotion(gfx::Context& ctx) {
    std::printf("reprojection follows the motion vectors\n");
    const auto pattern = [](int x, int y) {
        return 0.5f + 0.4f * std::sin(0.7f * static_cast<float>(x)) * std::cos(0.5f * static_cast<float>(y));
    };
    std::vector<Frame> frames;
    for (int k = 0; k < 8; ++k) {
        Frame frame = flatFrame();
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const float value = pattern(static_cast<int>(x) - 3 * k, static_cast<int>(y));
                float* l = frame.at(frame.lighting, x, y);
                l[0] = l[1] = l[2] = value;
                frame.at(frame.motion, x, y)[0] = -3.0f;
            }
        frames.push_back(frame);
    }
    Harness harness(ctx);
    const auto out = harness.run(frames, noFilter());
    const Output& last = out.back();
    double worst = 0.0;
    bool lengths = true;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 3; x < kSize - 3; ++x) {
            const float expected = pattern(static_cast<int>(x) - 21, static_cast<int>(y));
            for (std::uint32_t c = 0; c < 3; ++c)
                worst =
                    std::max(worst, static_cast<double>(std::fabs(last.hdr(x, y, c) - expected) / expected));
            const float length = std::min(8.0f, static_cast<float>(x / 3 + 1));
            lengths = lengths && last.length(x, y) == length;
        }
    std::printf("  worst relative error %.5f\n", worst);
    CHECK(worst < 0.02);
    CHECK(lengths);

    // The opposite sign mixes the wrong history in: the test would see it.
    for (Frame& frame : frames)
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x)
                frame.at(frame.motion, x, y)[0] = 3.0f;
    Harness wrong(ctx);
    const auto smeared = wrong.run(frames, noFilter());
    double wrongWorst = 0.0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 3; x < kSize - 3; ++x) {
            const float expected = pattern(static_cast<int>(x) - 21, static_cast<int>(y));
            wrongWorst =
                std::max(wrongWorst,
                         static_cast<double>(std::fabs(smeared.back().hdr(x, y, 0) - expected) / expected));
        }
    CHECK(wrongWorst > 0.1);
}

// Mean of the RGB output over one column.
double columnMean(const Output& out, std::uint32_t x) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t c = 0; c < 3; ++c)
            sum += out.hdr(x, y, c);
    return sum / (kSize * 3.0);
}

// Standard deviation of the RGB output around `mean` over columns [x0, x1), away from the top and bottom.
double flatStddev(const Output& out, std::uint32_t x0, std::uint32_t x1, double mean) {
    double sum = 0.0;
    int count = 0;
    for (std::uint32_t y = 8; y < kSize - 8; ++y)
        for (std::uint32_t x = x0; x < x1; ++x)
            for (std::uint32_t c = 0; c < 3; ++c) {
                const double d = out.hdr(x, y, c) - mean;
                sum += d * d;
                ++count;
            }
    return std::sqrt(sum / count);
}

// One stateless frame (every pixel at length 1, as at export below 16 spp) with a 0.1 | 0.9 lighting edge
// down the middle and noise of the given amplitude, filtered with 0 and with 3 à-trous iterations. Checks
// the two edge columns against 0.1 and 0.9 within the given tolerances and that the noise of the flat
// regions drops at least threefold.
void checkEdgeAndNoise(gfx::Context& ctx, float amplitude, bool depthStep, double leftTolerance,
                       double rightTolerance) {
    constexpr std::uint32_t kEdge = kSize / 2;
    Frame frame = flatFrame();
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float base = x < kEdge ? 0.1f : 0.9f;
            for (std::uint32_t c = 0; c < 3; ++c)
                frame.at(frame.lighting, x, y)[c] = base + 2.0f * amplitude * noise(7, x, y, c);
            frame.depth[std::size_t{y} * kSize + x] = depthStep && x >= kEdge ? 2.0f : 1.0f;
        }

    post::DenoiseSettings settings;
    settings.temporal = false;
    settings.atrousIterations = 0;
    Harness unfilteredHarness(ctx);
    const Output unfiltered = unfilteredHarness.runOne(frame, settings);
    settings.atrousIterations = 3;
    Harness filteredHarness(ctx);
    const Output filtered = filteredHarness.runOne(frame, settings);

    const double leftEdge = columnMean(filtered, kEdge - 1), rightEdge = columnMean(filtered, kEdge);
    std::printf("  edge columns %.4f | %.4f\n", leftEdge, rightEdge);
    CHECK(std::fabs(leftEdge - 0.1) <= leftTolerance);
    CHECK(std::fabs(rightEdge - 0.9) <= rightTolerance);
    for (const auto& [x0, x1, mean] :
         {std::tuple{8u, kEdge - 8, 0.1}, std::tuple{kEdge + 8, kSize - 8, 0.9}}) {
        const double before = flatStddev(unfiltered, x0, x1, mean),
                     after = flatStddev(filtered, x0, x1, mean);
        std::printf("  flat %u..%u: stddev %.5f -> %.5f (%.1fx)\n", x0, x1, before, after, before / after);
        CHECK(before / after >= 3.0);
    }
}

// (e) À-trous on one stateless frame: a 0.1 | 0.9 edge with a depth step and noise ±0.05 keeps both sides
// within 5 %, and the noise of the flat regions drops at least threefold.
void testAtrousKeepsEdgesAndRemovesNoise(gfx::Context& ctx) {
    std::printf("a-trous keeps a depth edge and removes noise\n");
    checkEdgeAndNoise(ctx, 0.05f, true, 0.005, 0.045);
}

// (e2) The same with nothing but the lighting to tell the sides apart (one depth, one normal: a shadow edge)
// and noise ±0.02: the short-history variance must not count the edge as noise, or every shadow edge at
// export would be blurred. Both edge columns within 10 %, flat noise down at least threefold.
void testAtrousKeepsLightingEdges(gfx::Context& ctx) {
    std::printf("a-trous keeps a lighting-only edge and removes noise\n");
    checkEdgeAndNoise(ctx, 0.02f, false, 0.01, 0.09);
}

// Reprojection between pixel centres: a linear ramp, then a flat frame with motion -1.5, -0.5 or -0.25. Each
// pixel's previous value is the ramp interpolated linearly between the two previous pixels its centre falls
// between, so the history at length 2 is exactly (bilinear(previous history) + current) / 2.
void testFractionalMotionInterpolates(gfx::Context& ctx) {
    std::printf("fractional motion interpolates the history bilinearly\n");
    Frame ramp = flatFrame();
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            float* l = ramp.at(ramp.lighting, x, y);
            l[0] = l[1] = l[2] = 0.25f + static_cast<float>(x) / 128.0f;
        }
    for (const float motion : {-1.5f, -0.5f, -0.25f}) {
        Frame flat = flatFrame();
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                float* l = flat.at(flat.lighting, x, y);
                l[0] = l[1] = l[2] = 0.5f;
                flat.at(flat.motion, x, y)[0] = motion;
            }
        Harness harness(ctx);
        const auto out = harness.run({ramp, flat}, noFilter());
        double worst = 0.0;
        bool lengths = true;
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 2; x < kSize; ++x) {
                const float base = static_cast<float>(x) + motion;
                const auto origin = static_cast<std::uint32_t>(std::floor(base));
                const float f = base - std::floor(base);
                for (std::uint32_t c = 0; c < 3; ++c) {
                    const float previous = (1.0f - f) * out[0].history(origin, y, c) +
                                           (f > 0.0f ? f * out[0].history(origin + 1, y, c) : 0.0f);
                    const float expected = 0.5f * (previous + 0.5f);
                    worst =
                        std::max(worst, static_cast<double>(std::fabs(out[1].history(x, y, c) - expected)));
                }
                lengths = lengths && out[1].length(x, y) == 2.0f;
            }
        std::printf("  motion %.2f: worst error %.6f\n", motion, worst);
        CHECK(worst <= 1.0 / 2048.0); // one ulp of [0.5, 1): the half conversion of the result
        CHECK(lengths);
    }
}

// The temporal variance (from length 4 on) estimates the variance of the samples: grey uniform noise in
// [0, 1) has luminance variance 1/12; the exponential moments with alpha 0.2 settle at 2 (1 - a) / (2 - a)
// of it (0.89), so after 32 frames the mean over the image is 1/12 within 30 %.
void testTemporalVariance(gfx::Context& ctx) {
    std::printf("temporal variance estimates the sample variance\n");
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 32; ++i) {
        Frame frame = noisyFrame(500 + i, 0.5f, 0.5f);
        for (std::size_t p = 0; p < std::size_t{kSize} * kSize; ++p)
            frame.lighting[p * 4 + 1] = frame.lighting[p * 4 + 2] = frame.lighting[p * 4]; // grey
        frames.push_back(frame);
    }
    Harness harness(ctx);
    const auto out = harness.run(frames, noFilter());
    double sum = 0.0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            sum += out.back().history(x, y, 3);
    const double mean = sum / (kSize * kSize);
    std::printf("  mean variance %.5f (1/12 = %.5f)\n", mean, 1.0 / 12.0);
    CHECK(std::fabs(mean - 1.0 / 12.0) <= 0.3 / 12.0);
}

// Bright illumination keeps a variance: grey 500 with uniform noise ±50 (variance 833) gives a positive
// variance at length 1 (spatial) and about the sample variance once the temporal moments take over. The
// squared luminance (250000) is beyond the half range, so the moments must not be stored as m2.
void testBrightVariance(gfx::Context& ctx) {
    std::printf("bright illumination keeps its variance\n");
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 8; ++i) {
        Frame frame = noisyFrame(600 + i, 500.0f, 50.0f);
        for (std::size_t p = 0; p < std::size_t{kSize} * kSize; ++p)
            frame.lighting[p * 4 + 1] = frame.lighting[p * 4 + 2] = frame.lighting[p * 4];
        frames.push_back(frame);
    }
    Harness harness(ctx);
    const auto out = harness.run(frames, noFilter());
    constexpr double kVariance = 100.0 * 100.0 / 12.0;
    for (const std::size_t i : {std::size_t{0}, std::size_t{7}}) {
        double sum = 0.0, smallest = 1e30;
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                sum += out[i].history(x, y, 3);
                smallest = std::min(smallest, static_cast<double>(out[i].history(x, y, 3)));
            }
        const double mean = sum / (kSize * kSize);
        std::printf("  frame %zu: mean variance %.1f, smallest %.1f (samples %.1f)\n", i + 1, mean, smallest,
                    kVariance);
        CHECK(smallest > 0.0);
        if (i == 7)
            CHECK(mean >= 0.5 * kVariance && mean <= 1.2 * kVariance);
    }
}

// A firefly decays: one 1e30 sample (demodulated and clamped to 60000) at a pixel with a long history
// enters with alpha 0.2 (12000) and then fades by 0.8 per frame. Below 0.5 + 1 takes ln(12000) / ln(1.25)
// = 42.1 frames, so 43 frames after it, nowhere in the history or output is above 1.5.
void testFireflyDecays(gfx::Context& ctx) {
    std::printf("a firefly decays out of the history\n");
    constexpr std::uint32_t kWarm = 8, kDecay = 43;
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < kWarm + 1 + kDecay; ++i) {
        Frame frame = flatFrame();
        for (float& v : frame.lighting)
            v = 0.5f;
        frames.push_back(frame);
    }
    float* firefly = frames[kWarm].at(frames[kWarm].lighting, 30, 30);
    firefly[0] = firefly[1] = firefly[2] = 1e30f;
    Harness harness(ctx);
    const auto out = harness.run(frames, {});
    const auto peak = [&](const Output& o, bool history) {
        float worst = 0.0f;
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x)
                for (std::uint32_t c = 0; c < 3; ++c)
                    worst = std::max(worst, history ? o.history(x, y, c) : o.hdr(x, y, c));
        return worst;
    };
    std::printf(
        "  firefly history: %.1f, then after 10 frames %.2f, after %u frames %.3f (output peak %.3f)\n",
        out[kWarm].history(30, 30, 0), out[kWarm + 10].history(30, 30, 0), kDecay,
        out.back().history(30, 30, 0), peak(out.back(), false));
    for (const Output& o : out) {
        CHECK(allFinite(o.hdrBits));
        CHECK(allFinite(o.historyBits));
    }
    // The decay follows 0.5 + 12000 * 0.8^k (within half precision).
    const double expected10 = 0.5 + 12000.0 * std::pow(0.8, 10);
    CHECK(std::fabs(out[kWarm + 10].history(30, 30, 0) - expected10) <= 0.01 * expected10);
    CHECK(peak(out.back(), true) < 1.5f);
    CHECK(peak(out.back(), false) < 1.5f);
}

// Non-finite guides never count as a match: a NaN normal or NaN depth in the previous frame's guides rejects
// that tap (length 1), and a NaN or infinite depth in the current frame is a miss that no neighbour filters
// with.
void testNonFiniteGuides(gfx::Context& ctx) {
    std::printf("non-finite guides are rejected\n");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    Frame first = flatFrame(), second = flatFrame();
    for (Frame* frame : {&first, &second})
        for (float& v : frame->lighting)
            v = 0.5f;
    first.at(first.normal, 10, 10)[0] = nan;
    first.depth[std::size_t{20} * kSize + 20] = nan;
    second.depth[std::size_t{40} * kSize + 40] = nan;
    second.depth[std::size_t{50} * kSize + 50] = inf;
    Harness harness(ctx);
    const auto out = harness.run({first, second, second}, {});
    for (const Output& o : out) {
        CHECK(allFinite(o.hdrBits));
        CHECK(allFinite(o.historyBits));
    }
    CHECK(out[1].length(10, 10) == 1.0f);
    CHECK(out[1].length(20, 20) == 1.0f);
    CHECK(out[2].length(40, 40) == 1.0f);
    CHECK(out[2].length(50, 50) == 1.0f);
    CHECK(out[1].length(11, 11) == 2.0f);
    // The neighbours of the non-finite depths are still the flat 0.5.
    for (const auto& [x, y] : {std::pair{41u, 40u}, std::pair{50u, 51u}})
        CHECK(std::fabs(out[2].hdr(x, y, 0) - 0.5f) <= 1e-3f);
}

// (f) The primary-hit emission bypasses history and filtering: a checker that flips every frame appears in
// the next output exactly as given.
void testEmissionPassesThrough(gfx::Context& ctx) {
    std::printf("emission passes through unfiltered and unaccumulated\n");
    const auto checker = [](std::uint32_t frameIndex, float lightingValue) {
        Frame frame = flatFrame();
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const bool on = (((x / 4) + (y / 4) + frameIndex) & 1u) != 0;
                float* e = frame.at(frame.emission, x, y);
                e[0] = on ? 3.0f : 0.25f;
                e[1] = on ? 0.5f : 2.0f;
                e[2] = on ? 0.0f : 1.0f;
                float* l = frame.at(frame.lighting, x, y);
                l[0] = l[1] = l[2] = lightingValue;
            }
        return frame;
    };
    for (const float lightingValue : {0.0f, 0.5f}) {
        std::vector<Frame> frames;
        for (std::uint32_t i = 0; i < 4; ++i)
            frames.push_back(checker(i, lightingValue));
        Harness harness(ctx);
        const auto out = harness.run(frames, {});
        for (std::uint32_t i = 0; i < 4; ++i) {
            Frame& frame = frames[i];
            bool exact = true;
            double worst = 0.0;
            for (std::uint32_t y = 0; y < kSize; ++y)
                for (std::uint32_t x = 0; x < kSize; ++x)
                    for (std::uint32_t c = 0; c < 3; ++c) {
                        const float e = frame.at(frame.emission, x, y)[c];
                        exact =
                            exact && out[i].hdrBits[(std::size_t{y} * kSize + x) * 4 + c] == floatToHalf(e);
                        worst = std::max(
                            worst, static_cast<double>(std::fabs(out[i].hdr(x, y, c) - (lightingValue + e))));
                    }
            if (lightingValue == 0.0f)
                CHECK(exact);
            else
                CHECK(worst <= 4e-3); // half precision of values up to 3.5
            // The history holds the illumination only.
            CHECK(std::fabs(out[i].history(9, 9, 0) - lightingValue) <= 1e-3);
        }
    }
}

// (g) One NaN, one +Inf and huge finite samples of either sign never make the output or the history
// non-finite, in the frame they arrive or later.
void testNonFiniteInputStaysOut(gfx::Context& ctx) {
    std::printf("NaN and Inf never reach output or history\n");
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 5; ++i)
        frames.push_back(noisyFrame(300 + i, 0.5f, 0.2f));
    float* nan = frames[1].at(frames[1].lighting, 10, 10);
    nan[0] = std::numeric_limits<float>::quiet_NaN();
    float* inf = frames[1].at(frames[1].lighting, 20, 20);
    inf[1] = std::numeric_limits<float>::infinity();
    frames[2].at(frames[2].lighting, 30, 30)[2] = 1e30f;  // beyond half range
    frames[2].at(frames[2].lighting, 35, 35)[1] = -1e30f; // and negative
    frames[3].at(frames[3].albedo, 40, 40)[0] = 0.0f;     // demodulation by zero
    frames[3].at(frames[3].lighting, 40, 40)[0] = 1e4f;   // illumination 1e7
    frames[3].at(frames[3].emission, 50, 50)[0] = std::numeric_limits<float>::quiet_NaN();

    Harness harness(ctx);
    const auto out = harness.run(frames, {});
    for (const Output& o : out) {
        CHECK(allFinite(o.hdrBits));
        CHECK(allFinite(o.historyBits));
        CHECK(allFinite(o.lengthBits));
    }
    // Radiance is never negative: the -1e30 sample counts as 0 (else it would store -65504 or -Inf).
    CHECK(out[2].history(35, 35, 1) >= 0.0f);
    CHECK(out[2].hdr(35, 35, 1) >= 0.0f);
    // The NaN pixel's lighting counted as 0: the next frames recover towards 0.5.
    CHECK(std::fabs(out[4].history(10, 10, 0) - 0.5f) < 0.3f);
}

// (h) The same input sequence twice gives the same bytes in every frame.
void testDeterministic(gfx::Context& ctx) {
    std::printf("the same inputs give the same bytes\n");
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 6; ++i) {
        Frame frame = noisyFrame(400 + i, 0.3f, 0.25f);
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                frame.at(frame.motion, x, y)[0] = -0.37f * static_cast<float>(i); // sub-pixel taps
                frame.depth[std::size_t{y} * kSize + x] = 1.0f + 0.01f * static_cast<float>(x);
            }
        frames.push_back(frame);
    }
    Harness a(ctx), b(ctx);
    const auto first = a.run(frames, {});
    const auto second = b.run(frames, {});
    for (std::size_t i = 0; i < frames.size(); ++i) {
        CHECK(first[i].hdrBits == second[i].hdrBits);
        CHECK(first[i].historyBits == second[i].historyBits);
        CHECK(first[i].lengthBits == second[i].lengthBits);
    }
}

// --- Task 7 of step 5: clean first frames, the export firefly filter, the volume layer
// ---------------------------------------------------------------------------------------------------------

// FNV-1a over the output and history bits of every frame.
std::uint64_t outputHash(const std::vector<Output>& outputs) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const Output& o : outputs)
        for (const auto* bits : {&o.hdrBits, &o.historyBits})
            for (const std::uint16_t v : *bits)
                for (int b = 0; b < 2; ++b) {
                    hash ^= (v >> (8 * b)) & 0xFFu;
                    hash *= 1099511628211ull;
                }
    return hash;
}

// Sparse lighting: each of `spp` samples per pixel is on with probability p at 1 / p, else 0 (mean 1).
Frame sparseFrame(std::uint32_t index, float p, std::uint32_t spp, std::uint32_t width = kSize,
                  std::uint32_t height = kSize) {
    Frame frame = flatFrame(width, height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (std::uint32_t s = 0; s < spp; ++s)
                sum += noise(index, x, y, 16 * s) + 0.5f < p ? 1.0f / p : 0.0f;
            float* l = frame.at(frame.lighting, x, y);
            l[0] = l[1] = l[2] = sum / static_cast<float>(spp);
        }
    return frame;
}

// The mean red lighting of a frame, and of an output's HDR image.
double imageMean(const Frame& frame) {
    double sum = 0.0;
    for (std::size_t i = 0; i < frame.lighting.size(); i += 4)
        sum += frame.lighting[i];
    return sum / static_cast<double>(frame.lighting.size() / 4);
}
double hdrMean(const Output& out) {
    double sum = 0.0;
    const std::uint32_t height = static_cast<std::uint32_t>(out.hdrBits.size() / 4 / out.width);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < out.width; ++x)
            sum += out.hdr(x, y, 0);
    return sum / (std::size_t{out.width} * height);
}

// (a) Sparse first frames: Bernoulli lighting (on with p = 0.3 at 1 / 0.3, else 0: mean 1) on one plane at
// length 1. The edge-robust variance sees all-zero quadrants and claims no noise, so step 4's denoiser left
// holes (RMSE 0.1833 at length 1 and 0.1346 at length 2 on this input, 3 iterations; the plan guessed
// ~0.063). Now sparse neighbourhoods get the plain variance with a first pass by geometry alone, and 5
// iterations at length <= 3: RMSE below 0.045, and no darker than the samples. Very bright sparse samples
// stay finite.
void testSparseFirstFrames(gfx::Context& ctx) {
    std::printf("sparse first frames are filtered clean\n");
    const auto bernoulli = [](std::uint32_t index, float p = 0.3f, std::uint32_t spp = 1) {
        return sparseFrame(index, p, spp);
    };
    Harness harness(ctx);
    const auto out = harness.run({bernoulli(900), bernoulli(901)}, {});
    CHECK(allLengths(out[0], 1.0f) && allLengths(out[1], 2.0f));
    const double first = rmse(out[0], 1.0f), second = rmse(out[1], 1.0f);
    std::printf("  RMSE after denoising: length 1 %.4f, length 2 %.4f (step 4: 0.1833, 0.1346)\n", first,
                second);
    CHECK(first < 0.045);
    CHECK(second < 0.045);
    // Not darker than the samples: a luminance stop on skewed sparse noise weights the rare bright samples
    // down (by 9 % on this frame with the plan's plain 3x3 variance: mean 0.906 for samples 0.989;
    // within 2 % now, the rest is the renormalised kernel at the image border). Also for sparser light: p =
    // 0.1 at 4 spp (66 % of the pixels 0).
    for (const auto& [p, spp] : {std::pair{0.3f, 1u}, std::pair{0.1f, 4u}}) {
        Harness sparse(ctx);
        const std::vector<Frame> frames = {bernoulli(900, p, spp), bernoulli(901, p, spp)};
        const auto result = sparse.run(frames, {});
        double accumulated = 0.0; // the mean of the samples the history holds (length 2: both frames, halved)
        std::uint32_t i = 0;
        for (const Frame& input : frames) {
            const double samples = imageMean(input);
            accumulated = i == 0 ? samples : 0.5 * (accumulated + samples);
            std::printf("  p %.1f at %u spp, length %u: mean %.4f, samples %.4f, RMSE %.4f\n", p, spp, i + 1,
                        hdrMean(result[i]), accumulated, rmse(result[i], 1.0f));
            CHECK(std::fabs(hdrMean(result[i]) - accumulated) < 0.02 * accumulated);
            ++i;
        }
    }

    // Bright sparse samples (the half range's top) keep the output and the history finite.
    Frame bright = bernoulli(902);
    for (float& v : bright.lighting)
        v *= 15000.0f;
    Harness brightHarness(ctx);
    for (const Output& o : brightHarness.run({bright, bright, bright}, {})) {
        CHECK(allFinite(o.hdrBits));
        CHECK(allFinite(o.historyBits));
    }

    // resetAtrousIterations at or below atrousIterations, on input that is not sparse, is step 4's denoiser:
    // the plan's (a) is then where the improvement comes from.
    post::DenoiseSettings step4;
    step4.resetAtrousIterations = 0;
    Harness old(ctx);
    const Output unchanged = old.runOne(noisyFrame(901, 0.5f, 0.2f), step4);
    Harness reset3(ctx);
    step4.resetAtrousIterations = 3;
    CHECK(reset3.runOne(noisyFrame(901, 0.5f, 0.2f), step4).hdrBits == unchanged.hdrBits);
}

// A lit | sparse-dark edge on one surface (one depth, one normal: a shadow edge at 1 spp): dense light of
// mean 1 (+-0.1) left of column 32, sparse light right of it (on with p = 0.1 at 0.5: mean 0.05, 90 % of the
// pixels 0). The sparse side's first a-trous pass has no luminance stop; the later ones must stop at the
// edge again, or the lit side bleeds into the dark one (removing `pc.step == 1u &&` in atrous.comp, geometry
// alone in every pass, makes the dark side 6 px from the edge several times too bright). Stateless frames,
// averaged over the rows of 4 frames: the dark side from 6 px off the edge within 20 % of its samples' mean,
// the whole frame's energy within 3 %. The softened edge is printed (README: 2-4 px on first frames).
void testSparseEdge(gfx::Context& ctx) {
    std::printf("a lit | sparse-dark edge stays sharp and keeps its energy\n");
    constexpr std::uint32_t kWidth = 64, kHeight = 192, kEdge = 32, kFrames = 4;
    std::vector<Frame> frames;
    for (std::uint32_t f = 0; f < kFrames; ++f) {
        Frame frame = flatFrame(kWidth, kHeight);
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const float v = x < kEdge ? 1.0f + 0.2f * noise(1200 + f, x, y, 1)
                                          : (noise(1200 + f, x, y, 0) + 0.5f < 0.1f ? 0.5f : 0.0f);
                float* l = frame.at(frame.lighting, x, y);
                l[0] = l[1] = l[2] = v;
            }
        frames.push_back(frame);
    }
    post::DenoiseSettings settings;
    settings.temporal = false;
    Harness harness(ctx, kWidth, kHeight);
    const std::vector<Output> out = harness.run(frames, settings);

    std::array<double, kWidth> output{}, input{};
    for (std::uint32_t f = 0; f < kFrames; ++f)
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                output[x] += out[f].hdr(x, y, 0) / (kFrames * kHeight);
                input[x] += frames[f].at(frames[f].lighting, x, y)[0] / (kFrames * kHeight);
            }
    const auto band = [](const std::array<double, kWidth>& columns, std::uint32_t x0, std::uint32_t x1) {
        double sum = 0.0;
        for (std::uint32_t x = x0; x < x1; ++x)
            sum += columns[x];
        return sum / (x1 - x0);
    };
    const double dark = band(input, kEdge, kWidth), lit = band(input, 0, kEdge);
    std::printf("  columns 26..41 (lit %.4f | dark %.4f):", lit, dark);
    for (std::uint32_t x = 26; x < 42; ++x)
        std::printf(" %.3f", output[x]);
    std::printf("\n");
    std::uint32_t softLit = 0, softDark = 0; // columns off their side's mean by more than 20 %
    for (std::uint32_t x = 0; x < kEdge; ++x)
        softLit += std::fabs(output[x] - lit) > 0.2 * lit ? 1u : 0u;
    for (std::uint32_t x = kEdge; x < kWidth; ++x)
        softDark += std::fabs(output[x] - dark) > 0.2 * dark ? 1u : 0u;
    const double near = band(output, kEdge + 6, kEdge + 12), far = band(output, kEdge + 12, kWidth);
    const double energy = band(output, 0, kWidth) / band(input, 0, kWidth);
    std::printf(
        "  soft edge: %u lit and %u dark columns off by > 20 %%; dark 6-12 px off the edge %.4f, beyond "
        "%.4f; energy ratio %.4f\n",
        softLit, softDark, near, far, energy);
    CHECK(std::fabs(near - dark) <= 0.2 * dark);
    CHECK(std::fabs(far - dark) <= 0.2 * dark);
    for (std::uint32_t x = kEdge + 6; x < kWidth; ++x)
        CHECK(std::fabs(output[x] - dark) <= 0.35 * dark); // one column: 768 samples, about 10 % noise
    CHECK(std::fabs(energy - 1.0) <= 0.03);
}

// Outliers in the history (the window's frames 2 and 3 after a reset): frame 1 holds lone bright samples (2 %
// of the pixels at 25 on 0.5 +- 0.05, like clamped indirect paths at 4 spp), frames 2 and 3 none. At length 2
// and 3 the history holds them at 1/2 and 1/3 weight, but the frame's spatial estimate cannot see them and
// the edge-robust minimum called the calm frame noise-free: each outlier stayed a speck (peak 12.77, 25x its
// surroundings, before this fix). The variance at length 2 and 3 now also takes the largest temporal variance
// in the 5x5 neighbourhood, so the outliers spread: no pixel above 2x the mean, and the mean within 8 % of
// the history's (measured -6.7 % and -4.6 %: low-variance pixels beyond the outliers' reach gather little of
// them; the history itself is unfiltered and keeps all of it).
void testHistoryOutliers(gfx::Context& ctx) {
    std::printf("outliers of earlier frames do not stay specks at length 2 and 3\n");
    std::vector<Frame> frames;
    double expected = 0.0;
    for (std::uint32_t f = 0; f < 3; ++f) {
        Frame frame = flatFrame();
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const bool outlier = f == 0 && noise(1400, x, y, 3) + 0.5f < 0.02f;
                const float v = outlier ? 25.0f : 0.5f + 0.1f * noise(1400 + f, x, y, 0);
                float* l = frame.at(frame.lighting, x, y);
                l[0] = l[1] = l[2] = v;
            }
        frames.push_back(frame);
    }
    Harness harness(ctx);
    const std::vector<Output> out = harness.run(frames, {});
    for (std::uint32_t f = 1; f < 3; ++f) {
        // The history's mean: frame 1 at 1 / (f + 1), the others alike.
        expected = 0.0;
        for (std::uint32_t i = 0; i <= f; ++i)
            expected += imageMean(frames[i]) / (f + 1);
        float peak = 0.0f;
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x)
                peak = std::max(peak, out[f].hdr(x, y, 0));
        std::printf("  length %u: peak %.3f, mean %.4f (history %.4f)\n", f + 1, peak, hdrMean(out[f]),
                    expected);
        CHECK(out[f].length(5, 5) == static_cast<float>(f + 1));
        CHECK(peak <= 2.0 * expected);
        CHECK(std::fabs(hdrMean(out[f]) - expected) <= 0.08 * expected);
    }
}

// Step 4's output, and no work without a medium. The step 4 denoiser's output on these frames (recorded at
// probe pixels, temporal and stateless, with no extra reset iterations) is matched within fp16 rounding on
// any driver; frames 2 and 3 of the temporal run are left out, since their variance now also counts the
// difference between frames (see accumulate.comp). hasMedium = false (no volume reads or passes) gives the
// same bytes as a (0, 0, 0, 1) volume layer read and filtered. And with albedo 1, no emission and no a-trous
// the output is the history, bit for bit.
void testNoMediumKeepsStep4Output(gfx::Context& ctx) {
    std::printf("no medium keeps the step 4 output\n");
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 6; ++i) {
        Frame frame = noisyFrame(700 + i, 0.3f, 0.25f);
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                frame.at(frame.motion, x, y)[0] = -0.37f * static_cast<float>(i);
                frame.depth[std::size_t{y} * kSize + x] = 1.0f + 0.01f * static_cast<float>(x);
                float* a = frame.at(frame.albedo, x, y);
                a[0] = 0.8f;
                a[1] = 0.5f;
                a[2] = 0.25f;
                frame.at(frame.emission, x, y)[0] = x % 7 == 0 ? 2.0f : 0.0f;
            }
        frames.push_back(frame);
    }
    post::DenoiseSettings temporal;
    temporal.resetAtrousIterations = 0;
    post::DenoiseSettings stateless = temporal;
    stateless.temporal = false;
    Harness a(ctx), b(ctx);
    const std::vector<Output> temporalOut = a.run(frames, temporal);
    const std::vector<Output> statelessOut = b.run(frames, stateless);

    // Recorded from the step 4 denoiser (llvmpipe): RGB at each probe pixel, per frame.
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> kProbes = {
        {{0, 0}, {5, 9}, {17, 33}, {31, 31}, {48, 7}, {63, 63}}};
    using ProbeTable = std::array<std::array<float, 18>, 6>;
    constexpr ProbeTable kTemporal = {{
        {2.335938f, 0.350098f, 0.247437f, 0.271973f, 0.302979f, 0.293213f, 0.305908f, 0.293213f, 0.311035f,
         0.307617f, 0.285645f, 0.298340f, 0.296387f, 0.287354f, 0.300781f, 2.291016f, 0.305664f, 0.297852f},
        {2.337891f, 0.294434f, 0.253174f, 0.276123f, 0.292725f, 0.307617f, 0.305176f, 0.286377f, 0.294189f,
         0.305664f, 0.300049f, 0.302490f, 0.289795f, 0.298340f, 0.299072f, 2.298828f, 0.319092f, 0.289307f},
        {2.322266f, 0.298584f, 0.278809f, 0.285400f, 0.288086f, 0.300781f, 0.307861f, 0.297119f, 0.295898f,
         0.303711f, 0.294922f, 0.299316f, 0.290527f, 0.300293f, 0.297607f, 2.292969f, 0.286133f, 0.291016f},
        {2.345703f, 0.276855f, 0.339844f, 0.289795f, 0.284912f, 0.300049f, 0.299805f, 0.296631f, 0.292725f,
         0.302490f, 0.303223f, 0.294678f, 0.293457f, 0.308350f, 0.298828f, 2.296875f, 0.275879f, 0.285889f},
        {2.277344f, 0.280518f, 0.270996f, 0.294678f, 0.290283f, 0.301758f, 0.295654f, 0.297607f, 0.291992f,
         0.304443f, 0.298584f, 0.297607f, 0.292236f, 0.301270f, 0.290283f, 2.308594f, 0.298584f, 0.297119f},
        {2.326172f, 0.330811f, 0.305176f, 0.294678f, 0.287354f, 0.292969f, 0.297363f, 0.303223f, 0.296875f,
         0.303955f, 0.301270f, 0.296875f, 0.292480f, 0.306885f, 0.288818f, 2.306641f, 0.308105f, 0.303223f},
    }};
    constexpr ProbeTable kStateless = {{
        {2.335938f, 0.350098f, 0.247437f, 0.271973f, 0.302979f, 0.293213f, 0.305908f, 0.293213f, 0.311035f,
         0.307617f, 0.285645f, 0.298340f, 0.296387f, 0.287354f, 0.300781f, 2.291016f, 0.305664f, 0.297852f},
        {2.339844f, 0.238403f, 0.255127f, 0.284668f, 0.281250f, 0.321289f, 0.306152f, 0.283936f, 0.281494f,
         0.305664f, 0.313477f, 0.310303f, 0.280762f, 0.314697f, 0.300781f, 2.322266f, 0.335449f, 0.275635f},
        {2.271484f, 0.305908f, 0.324951f, 0.283203f, 0.280762f, 0.298340f, 0.319092f, 0.292480f, 0.301514f,
         0.303467f, 0.286865f, 0.306396f, 0.299072f, 0.301270f, 0.282959f, 2.267578f, 0.266357f, 0.277832f},
        {2.339844f, 0.284912f, 0.338379f, 0.296875f, 0.278564f, 0.304443f, 0.283203f, 0.291260f, 0.293701f,
         0.298828f, 0.325439f, 0.284912f, 0.294678f, 0.320801f, 0.299561f, 2.283203f, 0.294434f, 0.288574f},
        {2.261719f, 0.281006f, 0.260010f, 0.310547f, 0.292725f, 0.322998f, 0.289551f, 0.312256f, 0.296875f,
         0.312012f, 0.271973f, 0.292236f, 0.292480f, 0.275635f, 0.297852f, 2.314453f, 0.292725f, 0.315430f},
        {2.332031f, 0.331055f, 0.316406f, 0.307373f, 0.299072f, 0.289795f, 0.305908f, 0.309570f, 0.294922f,
         0.299316f, 0.304199f, 0.293457f, 0.296875f, 0.323975f, 0.284424f, 2.304688f, 0.303223f, 0.333008f},
    }};
    // Within fp16 rounding: another driver may round each pass differently, a ulp or two through five passes
    // (2^-8 relative is 4 ulp; 2^-11 is 8 ulp at 0.3).
    const auto near = [](float value, float expected) {
        return std::fabs(value - expected) <= std::max(std::fabs(expected) * 0x1p-8f, 0x1p-11f);
    };
    double worst = 0.0;
    for (const auto& [outputs, table, skip] :
         {std::tuple{&temporalOut, &kTemporal, true}, std::tuple{&statelessOut, &kStateless, false}})
        for (std::uint32_t f = 0; f < 6; ++f) {
            if (skip && (f == 1 || f == 2))
                continue; // history length 2 and 3: see above
            for (std::size_t i = 0; i < kProbes.size(); ++i)
                for (std::uint32_t c = 0; c < 3; ++c) {
                    const float value = (*outputs)[f].hdr(kProbes[i].first, kProbes[i].second, c);
                    const float expected = (*table)[f][i * 3 + c];
                    worst = std::max(worst, static_cast<double>(std::fabs(value - expected)));
                    CHECK(near(value, expected));
                }
        }
    std::printf("  largest difference to step 4 at the probes: %.6f\n", worst);

    // No medium: the volume layer is neither read nor filtered (whatever it holds), which gives the same
    // bytes as a (0, 0, 0, 1) layer that is, temporal and stateless.
    std::vector<Frame> garbage = frames;
    for (Frame& frame : garbage)
        for (std::size_t i = 0; i < frame.volume.size(); ++i)
            frame.volume[i] = i % 4 == 3 ? 0.1f : 5.0f;
    for (post::DenoiseSettings settings : {temporal, stateless}) {
        settings.hasMedium = false;
        Harness skipped(ctx);
        const std::vector<Output> skippedOut = skipped.run(garbage, settings);
        const std::vector<Output>& withLayer = settings.temporal ? temporalOut : statelessOut;
        for (std::size_t f = 0; f < frames.size(); ++f) {
            CHECK(skippedOut[f].hdrBits == withLayer[f].hdrBits);
            CHECK(skippedOut[f].historyBits == withLayer[f].historyBits);
        }
    }
    // A medium that appears starts the volume history over: the layer of the frames without one is stale.
    post::DenoiseSettings none = temporal;
    none.hasMedium = false;
    Harness appears(ctx);
    appears.run({garbage[0], garbage[1]}, none);
    Frame fog = frames[2];
    for (std::size_t i = 0; i < fog.volume.size(); ++i)
        fog.volume[i] = i % 4 == 3 ? 0.5f : 0.25f;
    const Output first = appears.runOne(fog, temporal);
    CHECK(first.volumeLength(9, 9) == 1.0f && first.volume(9, 9, 0) == 0.25f &&
          first.volume(9, 9, 3) == 0.5f);

    Harness plain(ctx);
    const auto out = plain.run({noisyFrame(710, 0.5f, 0.4f), noisyFrame(711, 0.5f, 0.4f)}, noFilter());
    for (const Output& o : out)
        for (std::size_t i = 0; i < o.hdrBits.size(); ++i)
            if (i % 4 != 3)
                CHECK(o.hdrBits[i] == o.historyBits[i]);
}

// (c) The export firefly filter: a flat 0.5 field with one pixel at 500 comes out at most 4x its largest
// neighbour (the second largest of its 3x3 neighbourhood); a 2x2 block at 5 is kept within 1 %; glints on a
// geometry edge are kept; the emission is untouched; off (the default, as in the window) nothing is clamped.
void testFireflyFilter(gfx::Context& ctx) {
    std::printf("the export firefly filter clamps lone fireflies only\n");
    Frame frame = flatFrame();
    for (float& v : frame.lighting)
        v = 0.5f;
    float* firefly = frame.at(frame.lighting, 10, 10);
    firefly[0] = firefly[1] = firefly[2] = 500.0f;
    frame.at(frame.emission, 10, 10)[1] = 7.0f;
    for (std::uint32_t y = 40; y < 42; ++y)
        for (std::uint32_t x = 40; x < 42; ++x) {
            float* l = frame.at(frame.lighting, x, y);
            l[0] = l[1] = l[2] = 5.0f;
        }
    // A dotted line of glints along a depth edge (the RETURN shot's window sill at 64 spp: 1-pixel glints
    // 3-13 pixels apart, a line that fills in with more samples): kept.
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 55; x < kSize; ++x)
            frame.depth[std::size_t{y} * kSize + x] = 2.0f;
        if (y % 6 == 3) {
            float* l = frame.at(frame.lighting, 54, y);
            l[0] = l[1] = l[2] = 50.0f;
        }
    }

    post::DenoiseSettings settings = noFilter();
    settings.temporal = false;
    settings.fireflyFilter = true;
    Harness on(ctx);
    const Output filtered = on.runOne(frame, settings);
    std::printf("  firefly %.3f, block %.3f, edge glint %.3f\n", filtered.hdr(10, 10, 0),
                filtered.hdr(40, 40, 0), filtered.hdr(54, 9, 0));
    for (std::uint32_t y = 3; y < kSize; y += 6)
        CHECK(std::fabs(filtered.history(54, y, 0) - 50.0f) <= 50.0f / 1024.0f);
    for (std::uint32_t c = 0; c < 3; ++c) {
        CHECK(filtered.history(10, 10, c) <= 4.0f * 0.5f * (1.0f + 1.0f / 1024.0f));
        CHECK(filtered.history(10, 10, c) >= 4.0f * 0.5f * (1.0f - 1.0f / 1024.0f)); // clamped, not dropped
        for (std::uint32_t y = 40; y < 42; ++y)
            for (std::uint32_t x = 40; x < 42; ++x)
                CHECK(std::fabs(filtered.hdr(x, y, c) - 5.0f) <= 0.05f);
        CHECK(std::fabs(filtered.hdr(20, 20, c) - 0.5f) <= 1e-3f);
    }
    // The emission is added after the clamp: 2 + 7 in green.
    CHECK(std::fabs(filtered.hdr(10, 10, 1) - 9.0f) <= 9.0f / 1024.0f);
    // With a-trous on the firefly cannot spread either.
    settings.atrousIterations = 3;
    Harness spread(ctx);
    const Output smoothed = spread.runOne(frame, settings);
    float peak = 0.0f;
    for (std::uint32_t y = 0; y < 30; ++y)
        for (std::uint32_t x = 0; x < 30; ++x)
            peak = std::max(peak, smoothed.history(x, y, 0));
    CHECK(peak <= 2.0f * (1.0f + 1.0f / 1024.0f));

    Harness off(ctx);
    post::DenoiseSettings defaults = noFilter();
    CHECK(!defaults.fireflyFilter);
    CHECK(std::fabs(off.runOne(frame, defaults).history(10, 10, 0) - 500.0f) <= 0.5f);
}

// (d) The volume layer: hdr = T * surface + volume. A constant volume (0.2, 0.1, 0.05, T 0.5) over a surface
// of 1 gives exactly 0.5 + volume (converted to fp16), on every frame of a history; a noisy volume loses at
// least 3x of its standard deviation; the history follows the surface motion without depth or normal tests
// and is clamped to the current 3x3 min/max.
void testVolumeLayer(gfx::Context& ctx) {
    std::printf("the volume layer: composition, filter, history\n");
    const auto withVolume = [](Frame frame, float r, float g, float b, float t) {
        for (std::size_t i = 0; i < frame.volume.size() / 4; ++i) {
            frame.volume[i * 4] = r;
            frame.volume[i * 4 + 1] = g;
            frame.volume[i * 4 + 2] = b;
            frame.volume[i * 4 + 3] = t;
        }
        return frame;
    };
    Frame lit = flatFrame();
    for (float& v : lit.lighting)
        v = 1.0f;
    const Frame constant = withVolume(lit, 0.2f, 0.1f, 0.05f, 0.5f);
    Harness harness(ctx);
    const auto out = harness.run({constant, constant, constant, constant}, {});
    const float volume[3] = {0.2f, 0.1f, 0.05f};
    for (const Output& o : out)
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x)
                for (std::uint32_t c = 0; c < 3; ++c) {
                    const float expected = 0.5f + halfToFloat(floatToHalf(volume[c]));
                    CHECK(isHalfOf(o.hdrBits[(std::size_t{y} * kSize + x) * 4 + c], expected));
                }
    CHECK(out.back().volume(5, 5, 3) == 0.5f);
    CHECK(out.back().volumeLength(5, 5) == 4.0f);

    // Noise: radiance 0.2 +- 0.1 per pixel, T 0.5, no surface light; one stateless frame.
    Frame noisy = withVolume(flatFrame(), 0.2f, 0.2f, 0.2f, 0.5f);
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                noisy.at(noisy.volume, x, y)[c] += 0.2f * noise(950, x, y, c);
    post::DenoiseSettings settings;
    settings.temporal = false;
    settings.volumeAtrousIterations = 0;
    Harness raw(ctx);
    const Output unfiltered = raw.runOne(noisy, settings);
    settings.volumeAtrousIterations = 4;
    Harness smooth(ctx);
    const Output filtered = smooth.runOne(noisy, settings);
    const double before = flatStddev(unfiltered, 8, kSize - 8, 0.2),
                 after = flatStddev(filtered, 8, kSize - 8, 0.2);
    std::printf("  noisy volume: stddev %.5f -> %.5f (%.1fx)\n", before, after, before / after);
    CHECK(before / after >= 3.0);
    CHECK(filtered.volume(30, 30, 3) == 0.5f); // T is not filtered spatially

    // History: frame 1 volume 0.3; frame 2 a 0.2 | 0.4 checker, the right half at another depth (the surface
    // history is rejected there, the volume's is not). The previous 0.3 lies inside the 3x3 min/max, so at
    // length 2 the volume is (0.3 + current) / 2. Frame 3 at 0.9 everywhere: the history (<= 0.4) is clamped
    // up to 0.9, so the result is exactly 0.9.
    Frame first = withVolume(flatFrame(), 0.3f, 0.3f, 0.3f, 1.0f);
    Frame second = withVolume(flatFrame(), 0.0f, 0.0f, 0.0f, 1.0f);
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float v = (x + y) % 2 == 0 ? 0.2f : 0.4f;
            float* vol = second.at(second.volume, x, y);
            vol[0] = vol[1] = vol[2] = v;
            if (x >= kSize / 2)
                second.depth[std::size_t{y} * kSize + x] = 2.0f;
        }
    const Frame third = withVolume(flatFrame(), 0.9f, 0.9f, 0.9f, 1.0f);
    Harness history(ctx);
    const auto steps = history.run({first, second, third}, noFilter());
    bool halves = true, lengths = true, surfaceRejected = true, clamped = true;
    for (std::uint32_t y = 1; y < kSize - 1; ++y)
        for (std::uint32_t x = 1; x < kSize - 1; ++x) {
            const float v = (x + y) % 2 == 0 ? 0.2f : 0.4f;
            const float expected = 0.5f * (halfToFloat(floatToHalf(0.3f)) + halfToFloat(floatToHalf(v)));
            halves = halves && std::fabs(steps[1].volume(x, y, 0) - expected) <= 1.0f / 2048.0f;
            lengths = lengths && steps[1].volumeLength(x, y) == 2.0f;
            if (x >= kSize / 2)
                surfaceRejected = surfaceRejected && steps[1].length(x, y) == 1.0f;
            clamped = clamped && steps[2].volume(x, y, 0) == halfToFloat(floatToHalf(0.9f));
        }
    CHECK(halves);
    CHECK(lengths);
    CHECK(surfaceRejected);
    CHECK(clamped);
    // A reset copies the current volume exactly.
    history.denoiser().resetHistory();
    const Output reset = history.runOne(second, noFilter());
    CHECK(reset.volume(3, 4, 0) == halfToFloat(floatToHalf(0.4f)) && reset.volumeLength(3, 4) == 1.0f);
}

// A silhouette in fog: in-scattered light 0.4 in front of a far wall left of column 32 and 0.1 in front of a
// near object right of it, noise +-0.05, no surface light (so the output is the filtered layer); the two
// sides differ in transmittance (T 0.2 | 0.8, one depth), or in depth (1 | 0.5, one T). The volume a-trous
// stops at either step, so the step keeps its width: the two columns beside it within 10 %, while the flat
// noise still drops at least 3x. (A luminance stop alone smears it to 0.29 | 0.21: its standard deviation
// is largest at the step itself.)
void testVolumeEdge(gfx::Context& ctx) {
    std::printf("a volume step with a transmittance or depth step keeps its width\n");
    constexpr std::uint32_t kEdge = kSize / 2;
    for (const bool depthStep : {false, true}) {
        Frame frame = flatFrame();
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                float* v = frame.at(frame.volume, x, y);
                for (std::uint32_t c = 0; c < 3; ++c)
                    v[c] = (x < kEdge ? 0.4f : 0.1f) + 0.1f * noise(1300, x, y, c);
                v[3] = depthStep ? 0.5f : (x < kEdge ? 0.2f : 0.8f);
                if (depthStep && x >= kEdge)
                    frame.depth[std::size_t{y} * kSize + x] = 0.5f;
            }
        post::DenoiseSettings settings;
        settings.temporal = false;
        settings.volumeAtrousIterations = 0;
        Harness raw(ctx);
        const Output unfiltered = raw.runOne(frame, settings);
        settings.volumeAtrousIterations = 4;
        Harness smooth(ctx);
        const Output filtered = smooth.runOne(frame, settings);
        const double left = columnMean(filtered, kEdge - 1), right = columnMean(filtered, kEdge);
        std::printf("  %s step, columns 28..35:", depthStep ? "depth" : "T");
        for (std::uint32_t x = kEdge - 4; x < kEdge + 4; ++x)
            std::printf(" %.4f", columnMean(filtered, x));
        std::printf("\n");
        CHECK(std::fabs(left - 0.4) <= 0.04);
        CHECK(std::fabs(right - 0.1) <= 0.01);
        for (const auto& [x0, x1, mean] :
             {std::tuple{8u, kEdge - 8, 0.4}, std::tuple{kEdge + 8, kSize - 8, 0.1}}) {
            const double before = flatStddev(unfiltered, x0, x1, mean),
                         after = flatStddev(filtered, x0, x1, mean);
            std::printf("  flat %u..%u: stddev %.5f -> %.5f (%.1fx)\n", x0, x1, before, after,
                        before / after);
            CHECK(before / after >= 3.0);
        }
    }
}

// (e) Non-finite volume samples count as no medium: NaN or Inf radiance as 0, a non-finite transmittance as
// 1; negative radiance as 0 and a transmittance outside [0, 1] clamped. Nothing non-finite reaches the
// output or the histories, in the frame or later.
void testNonFiniteVolume(gfx::Context& ctx) {
    std::printf("non-finite volume samples stay out\n");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    std::vector<Frame> frames;
    for (std::uint32_t i = 0; i < 4; ++i) {
        Frame frame = noisyFrame(960 + i, 0.5f, 0.1f);
        for (std::size_t p = 0; p < frame.volume.size() / 4; ++p) {
            frame.volume[p * 4] = frame.volume[p * 4 + 1] = frame.volume[p * 4 + 2] = 0.1f;
            frame.volume[p * 4 + 3] = 0.8f;
        }
        frames.push_back(frame);
    }
    frames[1].at(frames[1].volume, 10, 10)[0] = nan;
    frames[1].at(frames[1].volume, 20, 20)[1] = inf;
    frames[1].at(frames[1].volume, 30, 30)[3] = nan;
    frames[1].at(frames[1].volume, 40, 40)[3] = inf;
    frames[2].at(frames[2].volume, 12, 12)[2] = -1e30f;
    frames[2].at(frames[2].volume, 14, 14)[3] = -3.0f;
    frames[2].at(frames[2].volume, 16, 16)[3] = 7.0f;
    Harness harness(ctx);
    const auto out = harness.run(frames, {});
    for (const Output& o : out) {
        CHECK(allFinite(o.hdrBits));
        CHECK(allFinite(o.volumeBits));
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x) {
                for (std::uint32_t c = 0; c < 3; ++c)
                    CHECK(o.volume(x, y, c) >= 0.0f && o.hdr(x, y, c) >= 0.0f);
                CHECK(o.volume(x, y, 3) >= 0.0f && o.volume(x, y, 3) <= 1.0f);
            }
    }
}

// Sizes, formats and recording are checked; resize() starts the history over at the new size.
void testRefusalsAndResize(gfx::Context& ctx) {
    std::printf("refusals and resize\n");
    Harness harness(ctx);
    post::GBufferView wrongHdr{harness.lighting_, harness.emission_, harness.albedo_, harness.normal_,
                               harness.depth_,    harness.motion_,   harness.volume_};
    // The HDR output must be RGBA16F, the lighting RGBA32F, every image the denoiser's size.
    CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(wrongHdr, harness.lighting_); }));
    post::GBufferView swapped{harness.emission_, harness.emission_, harness.albedo_, harness.normal_,
                              harness.depth_,    harness.motion_,   harness.volume_};
    CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(swapped, harness.hdr_); }));
    gfx::Image small =
        makeImage(ctx, 16, 16, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, "small");
    CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(wrongHdr, small); }));
    CHECK(throws<std::logic_error>([&] {
        post::GBufferView view{harness.lighting_, harness.emission_, harness.albedo_, harness.normal_,
                               harness.depth_,    harness.motion_,   harness.volume_};
        harness.denoiser().denoise(VK_NULL_HANDLE, view, {}, harness.hdr_); // not between begin and submit
    }));
    CHECK(throws<std::invalid_argument>([&] {
        gfx::FrameRing ring(ctx);
        post::Denoiser denoiser(ctx, ring, 0, 4);
    }));
    // Sigmas must be positive and finite, alphas in (0, 1].
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    post::GBufferView good{harness.lighting_, harness.emission_, harness.albedo_, harness.normal_,
                           harness.depth_,    harness.motion_,   harness.volume_};
    for (float post::DenoiseSettings::*field :
         {&post::DenoiseSettings::sigmaDepth, &post::DenoiseSettings::sigmaNormal,
          &post::DenoiseSettings::sigmaLuminance})
        for (const float bad : {0.0f, -1.0f, nan, inf}) {
            post::DenoiseSettings settings;
            settings.*field = bad;
            CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(good, harness.hdr_, settings); }));
        }
    for (float post::DenoiseSettings::*field :
         {&post::DenoiseSettings::alphaColor, &post::DenoiseSettings::alphaMoments})
        for (const float bad : {0.0f, -0.5f, 1.5f, nan, inf}) {
            post::DenoiseSettings settings;
            settings.*field = bad;
            CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(good, harness.hdr_, settings); }));
        }
    // At most 9 iterations of each kind; fireflyK finite and at least 1.
    for (std::uint32_t post::DenoiseSettings::*field :
         {&post::DenoiseSettings::atrousIterations, &post::DenoiseSettings::resetAtrousIterations,
          &post::DenoiseSettings::volumeAtrousIterations}) {
        post::DenoiseSettings settings;
        settings.*field = 10;
        CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(good, harness.hdr_, settings); }));
    }
    for (const float bad : {0.5f, 0.0f, -4.0f, nan, inf}) {
        post::DenoiseSettings settings;
        settings.fireflyK = bad;
        CHECK(throws<std::invalid_argument>([&] { harness.denoiseWith(good, harness.hdr_, settings); }));
    }
    post::DenoiseSettings alphaOne;
    alphaOne.alphaColor = alphaOne.alphaMoments = 1.0f;
    harness.denoiseWith(good, harness.hdr_, alphaOne); // 1 is allowed: no accumulation

    harness.run({noisyFrame(1, 0.5f, 0.1f), noisyFrame(2, 0.5f, 0.1f)}, {});
    harness.resize(32, 16);
    CHECK(harness.denoiser().historyLength().extent().width == 32);
    const Output resized = harness.runOne(flatFrame(32, 16), {});
    bool ones = true;
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 32; ++x)
            ones = ones && resized.length(x, y) == 1.0f;
    CHECK(ones);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    try {
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: the layer's synchronization validation is unavailable\n");
            // Between tests the queue goes idle: the installed layer's synchronization validation does not
            // credit host waits on the timeline semaphore, so memory VMA hands from one test's images to the
            // next would look like a hazard. Within a test nothing a frame uses is freed.
            const auto settle = [&] { vkQueueWaitIdle(ctx.queue()); };
            testNoiseConverges(ctx);
            settle();
            testResetMatchesFreshAndStateless(ctx);
            settle();
            testDisocclusionRejectsHistory(ctx);
            settle();
            testReprojectionFollowsMotion(ctx);
            settle();
            testAtrousKeepsEdgesAndRemovesNoise(ctx);
            settle();
            testAtrousKeepsLightingEdges(ctx);
            settle();
            testEmissionPassesThrough(ctx);
            settle();
            testFractionalMotionInterpolates(ctx);
            settle();
            testTemporalVariance(ctx);
            settle();
            testBrightVariance(ctx);
            settle();
            testFireflyDecays(ctx);
            settle();
            testNonFiniteGuides(ctx);
            settle();
            testNonFiniteInputStaysOut(ctx);
            settle();
            testDeterministic(ctx);
            settle();
            testRefusalsAndResize(ctx);
            settle();
            testSparseFirstFrames(ctx);
            settle();
            testSparseEdge(ctx);
            settle();
            testHistoryOutliers(ctx);
            settle();
            testNoMediumKeepsStep4Output(ctx);
            settle();
            testFireflyFilter(ctx);
            settle();
            testVolumeLayer(ctx);
            settle();
            testVolumeEdge(ctx);
            settle();
            testNonFiniteVolume(ctx);
            settle();
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("denoiser_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
