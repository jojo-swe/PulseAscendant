// post::Bloom on HDR pictures uploaded from the CPU, read back at half size and upsampled bilinearly on the
// CPU exactly as the tonemap reads it (the full-size PSF): a constant picture comes through within 1e-3
// relative (so (1 - w) I + w B = I), a single pixel of 1e4 keeps its energy within 0.5 % with 95 % of it
// within 64 pixels at 1920x1080, the spokes carry more than 0 and at most 8 % of the bloom and lie at 0, 60
// and 120 degrees with nothing extra at 45 or 90, the halo is round (the second moments along x and y agree
// within 2 %) without the anamorphic streak and wide with it, non-finite light counts as 0, the same picture
// gives the same bytes, and a wrong HDR image is refused. Reports the bloom's GPU time on this device. With
// the validation layer and its synchronization validation on; fails on any message.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/gpu_timers.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "post/bloom.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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

constexpr double kPi = 3.14159265358979323846;

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float magnitude;
    if (exponent == 0)
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        magnitude = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -magnitude : magnitude;
}

std::uint16_t floatToHalf(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    if (std::isnan(f))
        return static_cast<std::uint16_t>(sign | 0x7E00u);
    if (std::isinf(f))
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    const float magnitude = std::fabs(f);
    if (magnitude < std::ldexp(1.0f, -14))
        return static_cast<std::uint16_t>(sign | static_cast<std::uint32_t>(std::nearbyint(magnitude * 16777216.0f)));
    int exponent;
    std::frexp(magnitude, &exponent);
    auto mantissa = static_cast<std::uint32_t>(std::nearbyint(std::ldexp(magnitude, 11 - exponent)));
    int halfExponent = exponent - 1 + 15;
    if (mantissa == 2048) {
        mantissa = 1024;
        ++halfExponent;
    }
    return static_cast<std::uint16_t>(sign | (halfExponent << 10) | (mantissa & 0x3FFu));
}

// A grey picture (one value per pixel, the same in R, G and B).
struct Picture {
    std::uint32_t width, height;
    std::vector<float> v;
    Picture(std::uint32_t w, std::uint32_t h, float fill = 0.f) : width(w), height(h), v(std::size_t{w} * h, fill) {}
    float& at(std::uint32_t x, std::uint32_t y) { return v[std::size_t{y} * width + x]; }
    float at(std::uint32_t x, std::uint32_t y) const { return v[std::size_t{y} * width + x]; }
};

class Gpu {
public:
    Gpu(gfx::Context& ctx, std::uint32_t width, std::uint32_t height)
        : width_(width), height_(height),
          hdr_(ctx, {width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "test hdr"}),
          upload_(ctx, {std::size_t{width} * height * 8, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "test upload"}),
          download_(ctx, {std::size_t{(width + 1) / 2} * ((height + 1) / 2) * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          true, "test download"}),
          ring_(ctx), timers_(ctx, ring_, 4), bloom_(ctx, ring_, width, height) {}

    // The bloom picture (half size, R only) of a grey picture; all three channels must agree.
    Picture run(const Picture& in, float anamorphic = 0.f, float spokes = post::Bloom::kSpokeShare,
                bool poison = false) {
        auto* halves = static_cast<std::uint16_t*>(upload_.mapped());
        for (std::size_t i = 0; i < in.v.size(); ++i) {
            const std::uint16_t h = floatToHalf(in.v[i]);
            halves[i * 4 + 0] = h;
            halves[i * 4 + 1] = poison && i % 7 == 3 ? floatToHalf(std::numeric_limits<float>::quiet_NaN()) : h;
            halves[i * 4 + 2] = poison && i % 11 == 5 ? floatToHalf(std::numeric_limits<float>::infinity()) : h;
            halves[i * 4 + 3] = floatToHalf(1.f);
        }
        upload_.flush();
        VkCommandBuffer cmd = ring_.begin();
        hdr_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width_, height_, 1};
        vkCmdCopyBufferToImage(cmd, upload_.handle(), hdr_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        timers_.begin(cmd, "bloom");
        bloom_.record(cmd, hdr_, anamorphic, spokes);
        timers_.end(cmd);
        gfx::Image& out = bloom_.image();
        out.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT);
        // The previous run's copy into the download buffer is ordered before this one.
        gfx::bufferBarrier2(cmd, download_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy back{};
        back.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        back.imageExtent = {out.extent().width, out.extent().height, 1};
        vkCmdCopyImageToBuffer(cmd, out.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, download_.handle(), 1,
                               &back);
        ring_.wait(ring_.submit());
        timers_.collect();
        download_.invalidate();
        const auto* raw = static_cast<const std::uint16_t*>(download_.mapped());
        Picture result(out.extent().width, out.extent().height);
        for (std::size_t i = 0; i < result.v.size(); ++i) {
            const float r = halfToFloat(raw[i * 4]), g = halfToFloat(raw[i * 4 + 1]), b = halfToFloat(raw[i * 4 + 2]);
            if (!poison)
                CHECK(r == g && g == b);
            result.v[i] = r;
        }
        return result;
    }

    // The bloom's last measured GPU time in milliseconds (0 when the device has no timestamps).
    double lastMilliseconds() {
        for (const auto& [name, milliseconds] : timers_.lastCompleted())
            if (name == "bloom")
                return milliseconds;
        return 0.0;
    }

    post::Bloom& bloom() { return bloom_; }
    gfx::FrameRing& ring() { return ring_; }

private:
    std::uint32_t width_, height_;
    gfx::Image hdr_;
    gfx::Buffer upload_, download_;
    gfx::FrameRing ring_; // every run waits for its submit
    gfx::GpuTimers timers_;
    post::Bloom bloom_;
};

// The full-size PSF from the half-size bloom picture, as tonemap.comp's bloomAt reads it.
float fullAt(const Picture& half, double x, double y) {
    const double bx = x * 0.5 - 0.25, by = y * 0.5 - 0.25;
    const int ox = int(std::floor(bx)), oy = int(std::floor(by));
    const double fx = bx - ox, fy = by - oy;
    const auto tap = [&](int i, int j) {
        return half.at(std::uint32_t(std::clamp(i, 0, int(half.width) - 1)),
                       std::uint32_t(std::clamp(j, 0, int(half.height) - 1)));
    };
    return float((tap(ox, oy) * (1 - fx) + tap(ox + 1, oy) * fx) * (1 - fy) +
                 (tap(ox, oy + 1) * (1 - fx) + tap(ox + 1, oy + 1) * fx) * fy);
}

// The full-size PSF in a window of +-radius around (cx, cy).
struct Psf {
    int radius;
    std::vector<double> v;
    double at(int dx, int dy) const { return v[std::size_t(dy + radius) * (2 * radius + 1) + (dx + radius)]; }
};

Psf psfOf(const Picture& half, int cx, int cy, int radius) {
    Psf p{radius, std::vector<double>(std::size_t(2 * radius + 1) * (2 * radius + 1))};
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            p.v[std::size_t(dy + radius) * (2 * radius + 1) + (dx + radius)] = fullAt(half, cx + dx, cy + dy);
    return p;
}

double energy(const Psf& p, double withinRadius = 1e9) {
    double sum = 0;
    for (int dy = -p.radius; dy <= p.radius; ++dy)
        for (int dx = -p.radius; dx <= p.radius; ++dx)
            if (dx * dx + dy * dy <= withinRadius * withinRadius)
                sum += p.at(dx, dy);
    return sum;
}

// The PSF summed along a ray from r0 to r1 at an angle (bilinear between the PSF's pixels).
double ray(const Psf& p, double degrees, double r0, double r1) {
    const double c = std::cos(degrees * kPi / 180), s = std::sin(degrees * kPi / 180);
    double sum = 0;
    for (double r = r0; r <= r1; r += 0.5) {
        const double x = r * c, y = r * s;
        const int ix = int(std::floor(x)), iy = int(std::floor(y));
        const double fx = x - ix, fy = y - iy;
        sum += (p.at(ix, iy) * (1 - fx) + p.at(ix + 1, iy) * fx) * (1 - fy) +
               (p.at(ix, iy + 1) * (1 - fx) + p.at(ix + 1, iy + 1) * fx) * fy;
    }
    return sum;
}

constexpr std::uint32_t kW = 1920, kH = 1080;
constexpr int kCx = 960, kCy = 540;

// (a) A constant picture comes through: every bloom pixel within 1e-3 relative of the constant, so (1 - w) I
// + w B is I for any w.
void testConstant(Gpu& gpu) {
    const float value = 0.37f;
    const Picture out = gpu.run(Picture(kW, kH, value), 1.f); // the streak too
    double worst = 0;
    for (float v : out.v)
        worst = std::max(worst, double(std::abs(v - value) / value));
    std::printf("(a) constant %.2f: worst relative error %.2e (half precision step %.1e)\n", value, worst,
                std::ldexp(1.0, -11));
    CHECK(worst <= 1e-3);
}

// (b) One pixel of 1e4: the PSF keeps the energy within 0.5 %, 95 % of it lies within 64 pixels; (c) the
// spokes: more than 0 and at most 8 % of the bloom, along 0, 60 and 120 degrees and not 45 or 90; (d) round
// without the streak, wide with it.
void testPoint(Gpu& gpu) {
    Picture point(kW, kH);
    const float light = 1e4f;
    point.at(kCx, kCy) = light;
    constexpr int kR = 400;
    const Psf full = psfOf(gpu.run(point), kCx, kCy, kR);
    const Psf halo = psfOf(gpu.run(point, 0.f, 0.f), kCx, kCy, kR);

    const double total = energy(full), haloTotal = energy(halo);
    const double within64 = energy(full, 64) / total;
    std::printf("(b) point of %.0f: bloom energy %.1f (%+.3f %%), halo alone %.1f; within 8/16/32/64/128 px: "
                "%.3f %.3f %.3f %.3f %.3f\n",
                light, total, 100 * (total / light - 1), haloTotal, energy(full, 8) / total,
                energy(full, 16) / total, energy(full, 32) / total, within64, energy(full, 128) / total);
    CHECK(std::abs(total / light - 1) <= 0.005);
    CHECK(std::abs(haloTotal / light - 1) <= 0.005);
    CHECK(within64 >= 0.95);

    // The spokes: what the bloom holds beyond its scaled halo.
    double spokeEnergy = 0;
    Psf spokes{kR, std::vector<double>(full.v.size())};
    for (std::size_t i = 0; i < full.v.size(); ++i) {
        spokes.v[i] = full.v[i] - (1.0 - post::Bloom::kSpokeShare) * halo.v[i];
        spokeEnergy += spokes.v[i];
    }
    const double share = spokeEnergy / total;
    std::printf("(c) spokes: %.2f %% of the bloom\n", 100 * share);
    CHECK(share > 0.0 && share <= 0.08);
    // Along the rays from 24 to 128 pixels (outside the core, where a spoke is narrower than the gap to the
    // next angle tested): the bloom against its halo alone.
    const auto excess = [&](double degrees) {
        return ray(full, degrees, 24, 128) / ((1.0 - post::Bloom::kSpokeShare) * ray(halo, degrees, 24, 128));
    };
    std::printf("    bloom / halo along rays: ");
    for (double a : {0.0, 15.0, 30.0, 45.0, 60.0, 90.0, 120.0, 150.0, 180.0, 240.0, 300.0})
        std::printf("%g deg %.3f  ", a, excess(a));
    std::printf("\n");
    for (double a : {0.0, 60.0, 120.0, 180.0, 240.0, 300.0})
        CHECK(excess(a) > 1.1);
    const double base = excess(30.0);
    for (double a : {45.0, 90.0, 150.0, 270.0})
        CHECK(std::abs(excess(a) / base - 1) <= 0.03);

    // Round: the second moments along x and y within 2 % (three spokes 60 degrees apart are isotropic).
    const auto moments = [](const Psf& p) {
        double xx = 0, yy = 0;
        for (int dy = -p.radius; dy <= p.radius; ++dy)
            for (int dx = -p.radius; dx <= p.radius; ++dx) {
                xx += p.at(dx, dy) * dx * dx;
                yy += p.at(dx, dy) * dy * dy;
            }
        return std::pair<double, double>{xx, yy};
    };
    const auto [xx, yy] = moments(full);
    const Psf streak = psfOf(gpu.run(point, 1.f), kCx, kCy, kR);
    const auto [sxx, syy] = moments(streak);
    std::printf("(d) second moments x / y: %.1f / %.1f px^2 (%.2f %%); with the streak %.1f / %.1f, energy "
                "%+.3f %%\n",
                xx / total, yy / total, 100 * (xx / yy - 1), sxx / energy(streak), syy / energy(streak),
                100 * (energy(streak) / light - 1));
    CHECK(std::abs(xx / yy - 1) <= 0.02);
    CHECK(sxx > 2 * syy);
    CHECK(std::abs(energy(streak, 1e9) / light - 1) <= 0.03); // part of the streak reaches past the window
}

// (e) Non-finite light counts as 0; the same picture gives the same bytes.
void testSafeAndDeterministic(Gpu& gpu) {
    Picture ramp(kW, kH);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
            ramp.at(x, y) = float((x * 7 + y * 13) % 97) * 0.5f;
    const Picture a = gpu.run(ramp), b = gpu.run(ramp);
    CHECK(a.v == b.v);
    const Picture poisoned = gpu.run(ramp, 0.f, post::Bloom::kSpokeShare, true);
    for (float v : poisoned.v)
        CHECK(std::isfinite(v) && v >= 0.f);
}

// (f) The record() contract.
void testRejects(gfx::Context& ctx, Gpu& gpu) {
    gfx::Image wrongSize(ctx, {kW / 2, kH, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "small"});
    gfx::Image wrongFormat(ctx, {kW, kH, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "rgba8"});
    VkCommandBuffer cmd = gpu.ring().begin();
    int thrown = 0;
    for (gfx::Image* image : {&wrongSize, &wrongFormat}) {
        try {
            gpu.bloom().record(cmd, *image);
        } catch (const std::invalid_argument&) {
            ++thrown;
        }
    }
    gpu.ring().wait(gpu.ring().submit());
    CHECK(thrown == 2);
    CHECK(gpu.bloom().image().extent().width == kW / 2 && gpu.bloom().image().extent().height == kH / 2);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    try {
        auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the pictures are checked\n");
        else if (!ctx.syncValidationEnabled()) {
            std::printf("SKIP sync validation: the layer's synchronization validation is unavailable\n");
            return 0;
        }
        {
            Gpu gpu(ctx, kW, kH);
            testConstant(gpu);
            testPoint(gpu);
            testSafeAndDeterministic(gpu);
            testRejects(ctx, gpu);
            std::printf("bloom at %ux%u on this device: %.2f ms (GPU timer)\n", kW, kH, gpu.lastMilliseconds());
        }
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("bloom_test: %d checks passed in %.1f s\n", checksPassed,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return 0;
}
