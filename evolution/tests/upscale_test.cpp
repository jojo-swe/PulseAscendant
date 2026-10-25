// The temporal upscaler (Task 4 of step 5) on synthetic inputs uploaded from the CPU, 64x36 -> 128x72:
//   (a) at equal sizes the output equals the input byte for byte (Review Focus 3 still holds at scale 1);
//   (b) after a reset the output equals the pure spatial upsample (temporal off) byte for byte, whatever came
//       before, and that is the clamped Catmull-Rom upsample a CPU reference computes;
//   (c) a static high-frequency pattern traced with the per-frame subpixel window: after 16 frames the RMSE
//       against a native 128x72 render is below 0.8x the bilinear upsample's;
//   (d) a bar moving 2 output pixels per frame over a static background leaves no ghost wider than 2 pixels
//       after 8 frames;
//   (e) NaN and infinite inputs (colour, emission, depth, motion) give finite outputs and history;
//   (f) the emission stays out of the history: a screen whose picture changes every frame shows the current
//       frame's spatial upsample, where without the emission image it would ghost;
//   (g) the subpixel window: none at equal sizes, without history or on a reset frame, else one output pixel
//       wide and inside the trace pixel; determinism; refusals; resize resets.
// With the validation layer and its synchronization validation on; fails on any message.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "post/upscale.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
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

constexpr std::uint32_t kInW = 64, kInH = 36, kOutW = 128, kOutH = 72;

// --- Half floats -----------------------------------------------------------------------------------------

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

std::uint16_t floatToHalf(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    if (std::isnan(f))
        return static_cast<std::uint16_t>(sign | 0x7E00u);
    const float magnitude = std::fabs(f);
    if (magnitude >= 65520.0f)
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (magnitude < std::ldexp(1.0f, -14)) {
        const auto m = static_cast<std::uint32_t>(std::nearbyint(magnitude * 16777216.0f));
        return static_cast<std::uint16_t>(sign | m);
    }
    int exponent;
    std::frexp(magnitude, &exponent);
    const float scaled = std::ldexp(magnitude, 11 - exponent);
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

// --- Inputs ----------------------------------------------------------------------------------------------

// One frame's upscaler inputs at the trace size: RGBA colour, emission and motion, one depth per pixel.
struct Input {
    std::uint32_t width = kInW, height = kInH;
    std::vector<float> hdr, emission, motion, depth;
    float* at(std::vector<float>& image, std::uint32_t x, std::uint32_t y) {
        return &image[(std::size_t{y} * width + x) * 4];
    }
};

// Black, no emission, depth 1, valid zero motion.
Input flatInput(std::uint32_t width = kInW, std::uint32_t height = kInH) {
    Input in;
    in.width = width;
    in.height = height;
    const std::size_t pixels = std::size_t{width} * height;
    in.hdr.assign(pixels * 4, 0.f);
    in.emission.assign(pixels * 4, 0.f);
    in.motion.assign(pixels * 4, 0.f);
    in.depth.assign(pixels, 1.f);
    for (std::size_t i = 0; i < pixels; ++i) {
        in.hdr[i * 4 + 3] = 1.f;
        in.motion[i * 4 + 2] = 1.f;
    }
    return in;
}

using Pattern = std::function<float(double x, double y)>; // grey level at an output-pixel position

// The mean of a pattern over an axis-aligned box (output pixels), 8x8 samples.
double boxMean(const Pattern& f, double x0, double y0, double w, double h) {
    double sum = 0.0;
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i)
            sum += f(x0 + (i + 0.5) / 8.0 * w, y0 + (j + 0.5) / 8.0 * h);
    return sum / 64.0;
}

// What the path tracer gives at the trace size with a subpixel window: each trace pixel the mean over its
// window (in output pixels: centre (i + 0.5 + jitter) * scale, width footprint * scale).
Input tracedInput(const Pattern& f, const post::SubpixelWindow& window) {
    Input in = flatInput();
    const double sx = double(kOutW) / kInW, sy = double(kOutH) / kInH;
    for (std::uint32_t y = 0; y < kInH; ++y)
        for (std::uint32_t x = 0; x < kInW; ++x) {
            const double cx = (x + 0.5 + window.jitter[0]) * sx, cy = (y + 0.5 + window.jitter[1]) * sy;
            const double w = window.footprint[0] * sx, h = window.footprint[1] * sy;
            const auto v = static_cast<float>(boxMean(f, cx - w / 2, cy - h / 2, w, h));
            float* p = in.at(in.hdr, x, y);
            p[0] = p[1] = p[2] = v;
        }
    return in;
}

// A native render at the output size: each output pixel's box mean.
std::vector<double> nativeRender(const Pattern& f) {
    std::vector<double> out(std::size_t{kOutW} * kOutH);
    for (std::uint32_t y = 0; y < kOutH; ++y)
        for (std::uint32_t x = 0; x < kOutW; ++x)
            out[std::size_t{y} * kOutW + x] = boxMean(f, x, y, 1.0, 1.0);
    return out;
}

// --- The harness -----------------------------------------------------------------------------------------

struct Output {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint16_t> bits, historyBits; // RGBA16F
    float value(std::uint32_t x, std::uint32_t y, std::uint32_t c = 0) const {
        return halfToFloat(bits[(std::size_t{y} * width + x) * 4 + c]);
    }
    float history(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return halfToFloat(historyBits[(std::size_t{y} * width + x) * 4 + c]);
    }
};

// An upscaler on its own ring with input images, an output image and staging and readback buffers that live
// for the whole harness (synchronization validation never sees a reused allocation). One frame at a time.
class Harness {
public:
    Harness(gfx::Context& ctx, std::uint32_t inW = kInW, std::uint32_t inH = kInH, std::uint32_t outW = kOutW,
            std::uint32_t outH = kOutH)
        : ctx_(ctx), ring_(std::make_unique<gfx::FrameRing>(ctx)) {
        createImages(inW, inH, outW, outH);
        upscaler_ = std::make_unique<post::Upscaler>(ctx, *ring_, inW, inH, outW, outH);
    }
    ~Harness() {
        ring_->waitAll();
        upscaler_.reset();
        ring_.reset();
    }

    post::Upscaler& upscaler() { return *upscaler_; }

    Output run(const Input& in, const post::UpscaleSettings& settings, bool withEmission = false) {
        VkCommandBuffer cmd = ring_->begin();
        upload(cmd, in);
        upscaler_->record(cmd, hdr_, depth_, motion_, settings, out_, withEmission ? &emission_ : nullptr);
        readback(cmd);
        ring_->wait(ring_->submit());
        return collect();
    }

    void resize(std::uint32_t inW, std::uint32_t inH, std::uint32_t outW, std::uint32_t outH) {
        ring_->waitAll();
        vkQueueWaitIdle(ctx_.queue());
        upscaler_->resize(inW, inH, outW, outH);
        createImages(inW, inH, outW, outH);
    }

    // Records one upscale with the given images (to check refusals).
    void recordWith(gfx::Image& hdr, gfx::Image& depth, gfx::Image& motion, const post::UpscaleSettings& s,
                    gfx::Image& out) {
        VkCommandBuffer cmd = ring_->begin();
        try {
            upscaler_->record(cmd, hdr, depth, motion, s, out);
        } catch (...) {
            ring_->submit();
            throw;
        }
        ring_->submit();
    }

    gfx::Image hdr_, emission_, depth_, motion_, out_;

private:
    void createImages(std::uint32_t inW, std::uint32_t inH, std::uint32_t outW, std::uint32_t outH) {
        inW_ = inW, inH_ = inH, outW_ = outW, outH_ = outH;
        constexpr VkImageUsageFlags in = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        hdr_ = gfx::Image(ctx_, {inW, inH, VK_FORMAT_R16G16B16A16_SFLOAT, in, 1, "test hdr in"});
        emission_ = gfx::Image(ctx_, {inW, inH, VK_FORMAT_R16G16B16A16_SFLOAT, in, 1, "test emission"});
        motion_ = gfx::Image(ctx_, {inW, inH, VK_FORMAT_R16G16B16A16_SFLOAT, in, 1, "test motion"});
        depth_ = gfx::Image(ctx_, {inW, inH, VK_FORMAT_R32_SFLOAT, in, 1, "test depth"});
        out_ =
            gfx::Image(ctx_, {outW, outH, VK_FORMAT_R16G16B16A16_SFLOAT,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "test out"});
        const VkDeviceSize inPixels = VkDeviceSize{inW} * inH, outPixels = VkDeviceSize{outW} * outH;
        retired_.push_back(std::move(staging_));
        retired_.push_back(std::move(readback_));
        staging_ =
            gfx::Buffer(ctx_, {inPixels * (3 * 8 + 4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "staging"});
        readback_ = gfx::Buffer(ctx_, {outPixels * 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "readback"});
    }

    void upload(VkCommandBuffer cmd, const Input& in) {
        if (in.width != inW_ || in.height != inH_)
            throw std::logic_error("input size differs from the harness");
        auto* bytes = static_cast<std::uint8_t*>(staging_.mapped());
        VkDeviceSize offset = 0;
        const VkDeviceSize pixels = VkDeviceSize{inW_} * inH_;
        const auto copy = [&](gfx::Image& image, const void* data, VkDeviceSize size) {
            std::memcpy(bytes + offset, data, size);
            image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {inW_, inH_, 1};
            vkCmdCopyBufferToImage(cmd, staging_.handle(), image.handle(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            offset += size;
        };
        const auto halves = [](const std::vector<float>& values) {
            std::vector<std::uint16_t> out(values.size());
            for (std::size_t i = 0; i < values.size(); ++i)
                out[i] = floatToHalf(values[i]);
            return out;
        };
        const auto hdr = halves(in.hdr), emission = halves(in.emission), motion = halves(in.motion);
        copy(hdr_, hdr.data(), pixels * 8);
        copy(emission_, emission.data(), pixels * 8);
        copy(motion_, motion.data(), pixels * 8);
        copy(depth_, in.depth.data(), pixels * 4);
        staging_.flush();
    }

    void readback(VkCommandBuffer cmd) {
        const VkDeviceSize pixels = VkDeviceSize{outW_} * outH_;
        // The previous frame's copy into the buffer finished before the host read it; order the GPU writes
        // too.
        gfx::bufferBarrier2(cmd, readback_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        gfx::Image* sources[2] = {&out_, &upscaler_->history()};
        const int count = upscaler_->identity() ? 1 : 2;
        for (int i = 0; i < count; ++i) {
            sources[i]->transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                                   VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.bufferOffset = pixels * 8 * i;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {outW_, outH_, 1};
            vkCmdCopyImageToBuffer(cmd, sources[i]->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback_.handle(), 1, &region);
        }
        gfx::bufferBarrier2(cmd, readback_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
    }

    Output collect() {
        readback_.invalidate();
        const auto* bits = static_cast<const std::uint16_t*>(readback_.mapped());
        const std::size_t count = std::size_t{outW_} * outH_ * 4;
        Output out;
        out.width = outW_;
        out.height = outH_;
        out.bits.assign(bits, bits + count);
        if (!upscaler_->identity())
            out.historyBits.assign(bits + count, bits + 2 * count);
        return out;
    }

    gfx::Context& ctx_;
    std::uint32_t inW_ = 0, inH_ = 0, outW_ = 0, outH_ = 0;
    gfx::Buffer staging_, readback_;
    std::vector<gfx::Buffer> retired_;
    std::unique_ptr<gfx::FrameRing> ring_;
    std::unique_ptr<post::Upscaler> upscaler_;
};

post::UpscaleSettings temporal(const post::SubpixelWindow& window) {
    post::UpscaleSettings s;
    s.jitter = window.jitter;
    return s;
}

post::UpscaleSettings spatialOnly(const post::SubpixelWindow& window = {}) {
    post::UpscaleSettings s = temporal(window);
    s.temporal = false;
    return s;
}

post::SubpixelWindow windowOf(std::uint32_t frame) {
    return post::subpixelWindow(kInW, kInH, kOutW, kOutH, frame, true);
}

// --- CPU references --------------------------------------------------------------------------------------

double catmullRom(double t, int k) { // the weight of tap k (0..3) at fraction t
    const double t2 = t * t, t3 = t2 * t;
    switch (k) {
    case 0:
        return 0.5 * (-t3 + 2 * t2 - t);
    case 1:
        return 0.5 * (3 * t3 - 5 * t2 + 2);
    case 2:
        return 0.5 * (-3 * t3 + 4 * t2 + t);
    default:
        return 0.5 * (t3 - t2);
    }
}

// The clamped Catmull-Rom upsample of channel 0 at output pixel (x, y): the input sampled at its jittered
// grid (texel i at i + 0.5 + jitter), clamp-to-edge, then clamped to the 2x2 nearest texels.
double spatialReference(const Input& in, std::uint32_t x, std::uint32_t y, std::array<float, 2> jitter) {
    const double qx = (x + 0.5) * in.width / kOutW - 0.5 - jitter[0];
    const double qy = (y + 0.5) * in.height / kOutH - 0.5 - jitter[1];
    const int ix = static_cast<int>(std::floor(qx)), iy = static_cast<int>(std::floor(qy));
    const double tx = qx - ix, ty = qy - iy;
    const auto texel = [&](int i, int j) {
        i = std::clamp(i, 0, static_cast<int>(in.width) - 1);
        j = std::clamp(j, 0, static_cast<int>(in.height) - 1);
        return static_cast<double>(halfToFloat(floatToHalf(in.hdr[(std::size_t(j) * in.width + i) * 4])));
    };
    double sum = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            sum += catmullRom(tx, i) * catmullRom(ty, j) * texel(ix - 1 + i, iy - 1 + j);
    const double lo = std::min({texel(ix, iy), texel(ix + 1, iy), texel(ix, iy + 1), texel(ix + 1, iy + 1)});
    const double hi = std::max({texel(ix, iy), texel(ix + 1, iy), texel(ix, iy + 1), texel(ix + 1, iy + 1)});
    return std::clamp(sum, lo, hi);
}

// Bilinear upsample of channel 0 (texel centres, clamp-to-edge), the baseline of (c).
double bilinear(const Input& in, std::uint32_t x, std::uint32_t y) {
    const double qx = (x + 0.5) * in.width / kOutW - 0.5, qy = (y + 0.5) * in.height / kOutH - 0.5;
    const int ix = static_cast<int>(std::floor(qx)), iy = static_cast<int>(std::floor(qy));
    const double tx = qx - ix, ty = qy - iy;
    const auto texel = [&](int i, int j) {
        i = std::clamp(i, 0, static_cast<int>(in.width) - 1);
        j = std::clamp(j, 0, static_cast<int>(in.height) - 1);
        return static_cast<double>(in.hdr[(std::size_t(j) * in.width + i) * 4]);
    };
    return (texel(ix, iy) * (1 - tx) + texel(ix + 1, iy) * tx) * (1 - ty) +
           (texel(ix, iy + 1) * (1 - tx) + texel(ix + 1, iy + 1) * tx) * ty;
}

// RMSE over the interior (2 pixels in from every edge).
template <typename F> double interiorRmse(const std::vector<double>& reference, F&& value) {
    double sum = 0.0;
    int count = 0;
    for (std::uint32_t y = 2; y + 2 < kOutH; ++y)
        for (std::uint32_t x = 2; x + 2 < kOutW; ++x) {
            const double d = value(x, y) - reference[std::size_t{y} * kOutW + x];
            sum += d * d;
            ++count;
        }
    return std::sqrt(sum / count);
}

// --- Tests -----------------------------------------------------------------------------------------------

// A high-frequency pattern: a fine checker plus a product of sines, periods of 2.5 to 4 output pixels (beyond
// the trace size's Nyquist limit).
double busyPattern(double x, double y) {
    const int checker = (static_cast<int>(std::floor(x / 2.5)) + static_cast<int>(std::floor(y / 2.5))) & 1;
    return 0.3 + 0.4 * checker +
           0.2 * std::sin(2 * 3.14159265358979 * x / 3.3) * std::cos(2 * 3.14159265358979 * y / 4.1);
}

void testIdentity(gfx::Context& ctx) {
    Harness harness(ctx, kInW, kInH, kInW, kInH);
    CHECK(harness.upscaler().identity());
    Input in = tracedInput(busyPattern, {});
    for (std::uint32_t y = 0; y < kInH; ++y)
        for (std::uint32_t x = 0; x < kInW; ++x) {
            in.at(in.hdr, x, y)[1] = 0.01f * x;
            in.at(in.hdr, x, y)[2] = 100.f + y;
            in.at(in.motion, x, y)[0] = 0.5f; // ignored at equal sizes
        }
    for (int frame = 0; frame < 3; ++frame) {
        const Output out = harness.run(in, temporal(windowOf(frame)));
        for (std::uint32_t y = 0; y < kInH; ++y)
            for (std::uint32_t x = 0; x < kInW; ++x)
                for (std::uint32_t c = 0; c < 4; ++c)
                    CHECK(out.bits[(std::size_t{y} * kInW + x) * 4 + c] ==
                          floatToHalf(in.hdr[(std::size_t{y} * kInW + x) * 4 + c]));
    }
    std::printf("(a) identity at equal sizes: byte for byte\n");
}

void testResetIsSpatial(gfx::Context& ctx) {
    Harness harness(ctx);
    // Some history first: other pictures, jittered.
    for (std::uint32_t frame = 0; frame < 5; ++frame)
        harness.run(
            tracedInput([&](double x, double y) { return busyPattern(x + frame, y); }, windowOf(frame)),
            temporal(windowOf(frame)));
    const post::SubpixelWindow window = windowOf(3); // any jitter: the spatial term follows it
    const Input in = tracedInput(busyPattern, window);
    harness.upscaler().resetHistory();
    const Output afterReset = harness.run(in, temporal(window));
    Harness fresh(ctx);
    const Output spatial = fresh.run(in, spatialOnly(window));
    CHECK(afterReset.bits == spatial.bits);
    // The history keeps the frame with weight 1, and a temporal-off frame never reads the history.
    for (std::uint32_t y = 0; y < kOutH; ++y)
        for (std::uint32_t x = 0; x < kOutW; ++x)
            CHECK(afterReset.history(x, y, 3) == 1.f &&
                  afterReset.history(x, y, 0) == afterReset.value(x, y));
    const Output again = harness.run(in, spatialOnly(window));
    CHECK(again.bits == spatial.bits);

    double worst = 0.0;
    for (std::uint32_t y = 0; y < kOutH; ++y)
        for (std::uint32_t x = 0; x < kOutW; ++x)
            worst =
                std::max(worst, std::fabs(spatial.value(x, y) - spatialReference(in, x, y, window.jitter)));
    std::printf("(b) after a reset: the spatial upsample; worst difference from the CPU Catmull-Rom %.5f\n",
                worst);
    CHECK(worst < 2e-3);
}

void testStaticPatternConverges(gfx::Context& ctx) {
    const std::vector<double> native = nativeRender(busyPattern);
    const Input lowRes = tracedInput(busyPattern, {}); // a plain render at the trace size
    const double bilinearRmse = interiorRmse(native, [&](auto x, auto y) { return bilinear(lowRes, x, y); });

    Harness harness(ctx);
    Output out;
    for (std::uint32_t frame = 0; frame < 16; ++frame)
        out = harness.run(tracedInput(busyPattern, windowOf(frame)), temporal(windowOf(frame)));
    const double upscaledRmse = interiorRmse(native, [&](auto x, auto y) { return out.value(x, y); });
    const Output first = Harness(ctx).run(lowRes, spatialOnly());
    const double spatialRmse = interiorRmse(native, [&](auto x, auto y) { return first.value(x, y); });
    std::printf(
        "(c) static pattern, RMSE against native 128x72: bilinear %.4f, spatial %.4f, temporal after 16 "
        "frames %.4f (%.2fx bilinear)\n",
        bilinearRmse, spatialRmse, upscaledRmse, upscaledRmse / bilinearRmse);
    CHECK(upscaledRmse < 0.8 * bilinearRmse);
}

void testMovingBarLeavesNoGhost(gfx::Context& ctx) {
    constexpr double kBackground = 0.1, kBar = 1.0, kBarWidth = 10.0, kStart = 30.0;
    const auto barLeft = [&](std::uint32_t frame) { return kStart + 2.0 * frame; }; // output pixels
    const auto pattern = [&](std::uint32_t frame) {
        return [=](double x, double) {
            return x >= barLeft(frame) && x < barLeft(frame) + kBarWidth ? kBar : kBackground;
        };
    };
    Harness harness(ctx);
    Output out;
    int ghostWidth = 0;
    for (std::uint32_t frame = 0; frame < 8; ++frame) {
        const post::SubpixelWindow window = windowOf(frame);
        Input in = tracedInput(pattern(frame), window);
        // The bar is nearer and moves 2 output pixels (1 trace pixel) right per frame: where it was is 1
        // trace pixel left. The background stands still.
        for (std::uint32_t y = 0; y < kInH; ++y)
            for (std::uint32_t x = 0; x < kInW; ++x) {
                const double centre = (x + 0.5 + window.jitter[0]) * 2.0;
                const bool bar = centre >= barLeft(frame) && centre < barLeft(frame) + kBarWidth;
                in.at(in.motion, x, y)[0] = bar ? -1.f : 0.f;
                in.depth[std::size_t{y} * kInW + x] = bar ? 1.f : 5.f;
            }
        out = harness.run(in, temporal(window));
    }
    // A ghost: an output pixel off the native picture by more than 10 % of the contrast, farther than its
    // distance from the bar's edges allows.
    const std::vector<double> native = nativeRender(pattern(7));
    const double left = barLeft(7), right = barLeft(7) + kBarWidth;
    for (std::uint32_t y = 2; y + 2 < kOutH; ++y)
        for (std::uint32_t x = 2; x + 2 < kOutW; ++x) {
            const double error = std::fabs(out.value(x, y) - native[std::size_t{y} * kOutW + x]);
            if (error <= 0.1 * (kBar - kBackground))
                continue;
            const double centre = x + 0.5;
            const double fromEdge = std::min(std::fabs(centre - left), std::fabs(centre - right));
            ghostWidth = std::max(ghostWidth, static_cast<int>(std::ceil(fromEdge - 0.5)));
        }
    std::printf("(d) bar moving 2 px per frame: widest error band beside an edge after 8 frames %d px\n",
                ghostWidth);
    CHECK(ghostWidth <= 2);

    // The reprojection itself (the clip box above hides a wrong motion at a sharp edge): the whole textured
    // picture pans 2 output pixels per frame, every pixel's motion 1 trace pixel. Following it, the history
    // keeps converging as for a still picture.
    const auto panned = [](std::uint32_t frame) {
        return [=](double x, double y) { return busyPattern(x - 2.0 * frame, y); };
    };
    Harness pan(ctx);
    for (std::uint32_t frame = 0; frame < 12; ++frame) {
        Input in = tracedInput(panned(frame), windowOf(frame));
        for (std::uint32_t y = 0; y < kInH; ++y)
            for (std::uint32_t x = 0; x < kInW; ++x)
                in.at(in.motion, x, y)[0] = -1.f;
        out = pan.run(in, temporal(windowOf(frame)));
    }
    const std::vector<double> nativePan = nativeRender(panned(11));
    const Input lowRes = tracedInput(panned(11), {});
    // The left edge shows new picture every frame: only x >= 24 has a long history.
    double sum = 0.0, bilinearSum = 0.0;
    for (std::uint32_t y = 2; y + 2 < kOutH; ++y)
        for (std::uint32_t x = 24; x + 2 < kOutW; ++x) {
            const double want = nativePan[std::size_t{y} * kOutW + x];
            sum += (out.value(x, y) - want) * (out.value(x, y) - want);
            bilinearSum += (bilinear(lowRes, x, y) - want) * (bilinear(lowRes, x, y) - want);
        }
    std::printf("    panning texture after 12 frames: RMSE %.2fx the bilinear upsample's\n",
                std::sqrt(sum / bilinearSum));
    CHECK(sum < 0.8 * 0.8 * bilinearSum);
}

void testNonFinite(gfx::Context& ctx) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    Harness harness(ctx);
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        Input in = tracedInput(busyPattern, windowOf(frame));
        for (std::uint32_t i = 0; i < 40; ++i) {
            const std::uint32_t x = (i * 13 + frame * 7) % kInW, y = (i * 7 + frame) % kInH;
            const float bad = (i & 1) ? nan : (i & 2) ? inf : -inf;
            in.at(in.hdr, x, y)[i % 4] = bad;
            in.at(in.emission, (x + 3) % kInW, y)[i % 3] = bad;
            in.at(in.motion, (x + 5) % kInW, y)[i % 2] = bad;
            in.depth[std::size_t{(y + 2) % kInH} * kInW + x] = bad;
        }
        const Output out = harness.run(in, temporal(windowOf(frame)), true);
        for (std::size_t i = 0; i < out.bits.size(); ++i) {
            CHECK(std::isfinite(halfToFloat(out.bits[i])));
            CHECK(std::isfinite(halfToFloat(out.historyBits[i])));
        }
    }
    std::printf("(e) NaN and infinite inputs: finite output and history\n");
}

// A screen: the left half of the picture is an emitter whose picture flips between two patterns every frame
// (lighting 0 there, so hdr = emission); the right half a static lit surface.
Input screenInput(std::uint32_t frame) {
    Input in = flatInput();
    for (std::uint32_t y = 0; y < kInH; ++y)
        for (std::uint32_t x = 0; x < kInW; ++x) {
            float* hdr = in.at(in.hdr, x, y);
            if (x < kInW / 2) {
                const bool lit = ((x / 2 + y / 3 + frame) & 1) != 0;
                const float e = lit ? 2.f : 0.1f;
                float* emission = in.at(in.emission, x, y);
                emission[0] = emission[1] = emission[2] = e;
                hdr[0] = hdr[1] = hdr[2] = e;
            } else {
                hdr[0] = hdr[1] = hdr[2] = 0.4f;
            }
        }
    return in;
}

void testEmissionStaysCurrent(gfx::Context& ctx) {
    Harness withEmission(ctx), without(ctx);
    double worstWith = 0.0, worstWithout = 0.0;
    for (std::uint32_t frame = 0; frame < 6; ++frame) {
        const post::SubpixelWindow window = windowOf(frame);
        const Input in = screenInput(frame);
        const Output a = withEmission.run(in, temporal(window), true);
        const Output b = without.run(in, temporal(window), false);
        const Output current = Harness(ctx).run(in, spatialOnly(window), true);
        if (frame == 0)
            continue;
        for (std::uint32_t y = 2; y + 2 < kOutH; ++y)
            for (std::uint32_t x = 2; x < kOutW / 2 - 2; ++x) {
                const double want = current.value(x, y);
                worstWith = std::max(worstWith, std::fabs(a.value(x, y) - want) / want);
                worstWithout = std::max(worstWithout, std::fabs(b.value(x, y) - want) / want);
            }
    }
    std::printf(
        "(f) a screen flipping every frame: worst relative difference from the current frame %.4f with "
        "the emission image, %.4f without\n",
        worstWith, worstWithout);
    CHECK(worstWith < 1e-3);
    CHECK(worstWithout > 0.5); // without it the history smears the screen
}

void testWindowAndRefusals(gfx::Context& ctx) {
    // The subpixel window.
    for (const std::uint32_t frame : {0u, 1u, 7u, 40u}) {
        const post::SubpixelWindow same = post::subpixelWindow(64, 36, 64, 36, frame, true);
        CHECK(same.jitter == (std::array<float, 2>{0.f, 0.f}) &&
              same.footprint == (std::array<float, 2>{1.f, 1.f}));
        const post::SubpixelWindow exported = post::subpixelWindow(64, 36, 128, 72, frame, false);
        CHECK(exported.jitter == (std::array<float, 2>{0.f, 0.f}) &&
              exported.footprint == (std::array<float, 2>{1.f, 1.f}));
    }
    CHECK(windowOf(0).jitter == (std::array<float, 2>{0.f, 0.f}) &&
          windowOf(0).footprint == (std::array<float, 2>{1.f, 1.f}));
    std::array<float, 2> sum{0.f, 0.f};
    bool moved = false;
    for (std::uint32_t frame = 1; frame <= 16; ++frame) {
        const post::SubpixelWindow w = windowOf(frame);
        CHECK(w.footprint == (std::array<float, 2>{0.5f, 0.5f}));
        for (int a = 0; a < 2; ++a) {
            CHECK(std::fabs(w.jitter[a]) + w.footprint[a] / 2 <= 0.5f + 1e-6f); // inside the trace pixel
            sum[a] += w.jitter[a];
        }
        moved = moved || w.jitter != windowOf(1).jitter;
        CHECK(w.jitter == windowOf(frame + 16).jitter); // 16 frames long
    }
    CHECK(moved && std::fabs(sum[0]) < 0.1f && std::fabs(sum[1]) < 0.1f); // centred over a cycle
    const post::SubpixelWindow odd = post::subpixelWindow(86, 48, 128, 72, 3, true);
    CHECK(odd.footprint[0] == 86.f / 128.f && odd.footprint[1] == 48.f / 72.f);

    // Determinism: the same frames give the same bytes in a fresh upscaler.
    std::vector<Output> runs[2];
    for (auto& run : runs) {
        Harness harness(ctx);
        for (std::uint32_t frame = 0; frame < 4; ++frame)
            run.push_back(harness.run(screenInput(frame), temporal(windowOf(frame)), true));
    }
    for (std::size_t i = 0; i < runs[0].size(); ++i)
        CHECK(runs[0][i].bits == runs[1][i].bits && runs[0][i].historyBits == runs[1][i].historyBits);

    // Refusals.
    CHECK(throws<std::invalid_argument>(
        [&] { post::Upscaler(ctx, *std::make_unique<gfx::FrameRing>(ctx), 64, 36, 32, 72); }));
    CHECK(throws<std::invalid_argument>(
        [&] { post::Upscaler(ctx, *std::make_unique<gfx::FrameRing>(ctx), 0, 36, 64, 72); }));
    Harness harness(ctx);
    post::UpscaleSettings bad;
    bad.feedback = 1.f;
    CHECK(throws<std::invalid_argument>(
        [&] { harness.recordWith(harness.hdr_, harness.depth_, harness.motion_, bad, harness.out_); }));
    bad = {};
    bad.jitter = {0.75f, 0.f};
    CHECK(throws<std::invalid_argument>(
        [&] { harness.recordWith(harness.hdr_, harness.depth_, harness.motion_, bad, harness.out_); }));
    CHECK(throws<std::invalid_argument>(
        [&] { harness.recordWith(harness.hdr_, harness.depth_, harness.motion_, {}, harness.hdr_); }));
    CHECK(throws<std::invalid_argument>(
        [&] { harness.recordWith(harness.hdr_, harness.motion_, harness.motion_, {}, harness.out_); }));
    CHECK(throws<std::logic_error>([&] {
        harness.upscaler().record(VK_NULL_HANDLE, harness.hdr_, harness.depth_, harness.motion_, {},
                                  harness.out_);
    }));

    // A resize resets: the first frame at the new size is the spatial upsample.
    harness.run(screenInput(0), temporal(windowOf(0)));
    harness.run(screenInput(1), temporal(windowOf(1)));
    harness.resize(48, 27, 96, 54);
    Input small = flatInput(48, 27);
    for (std::uint32_t y = 0; y < 27; ++y)
        for (std::uint32_t x = 0; x < 48; ++x)
            small.at(small.hdr, x, y)[0] = 0.05f * ((x * 7 + y * 3) % 11);
    const Output resized = harness.run(small, temporal({}));
    Harness reference(ctx, 48, 27, 96, 54);
    CHECK(resized.bits == reference.run(small, spatialOnly()).bits);
    std::printf("(g) subpixel window, determinism, refusals and resize\n");
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
            // The queue goes idle between tests (the layer does not credit timeline waits; see
            // denoiser_test).
            const auto settle = [&] { vkQueueWaitIdle(ctx.queue()); };
            testIdentity(ctx);
            settle();
            testResetIsSpatial(ctx);
            settle();
            testStaticPatternConverges(ctx);
            settle();
            testMovingBarLeavesNoGhost(ctx);
            settle();
            testNonFinite(ctx);
            settle();
            testEmissionStaysCurrent(ctx);
            settle();
            testWindowAndRefusals(ctx);
            settle();
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("upscale_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
