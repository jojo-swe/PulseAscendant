// The shared descriptor layer (layouts, per-slot allocators, checked writes including acceleration
// structures), ComputePipeline on top of it, the ray tracing properties the Context reads from the driver,
// aligned and staged device buffers, and sync validation as a Context option. Runs with the validation layer
// on and fails on any message except the one hazard the sync validation case records on purpose.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/descriptors.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/paths.hpp"
#include "gfx/sync.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace evo::gfx;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// Points shaderDir() at the build's compiled shaders.
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

template <typename F> bool throwsInvalidArgument(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument& error) {
        std::printf("  refused: %s\n", error.what());
        return true;
    }
    return false;
}

const std::filesystem::path kProbe = EVO_TEST_SPIRV_DIR "/test_descriptor_probe.comp.spv";

// The probe's set: three textures through one array, an input and an output storage buffer, a storage image.
constexpr std::array<Binding, 4> kProbeBindings = {
    Binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
    Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    Binding{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};

// A top-level acceleration structure sized for one instance and never built: enough for a descriptor
// write, which only needs the handle.
class UnbuiltTlas {
public:
    explicit UnbuiltTlas(Context& ctx) : ctx_(ctx) {
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        VkAccelerationStructureBuildGeometryInfoKHR build{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        const std::uint32_t instances = 1;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(ctx.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &build, &instances, &sizes);
        storage_ = Buffer(ctx, {sizes.accelerationStructureSize,
                                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                false, "unbuilt tlas"});
        VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        info.buffer = storage_.handle();
        info.size = sizes.accelerationStructureSize;
        info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (vkCreateAccelerationStructureKHR(ctx.device(), &info, nullptr, &handle_) != VK_SUCCESS)
            throw std::runtime_error("vkCreateAccelerationStructureKHR");
    }
    ~UnbuiltTlas() { vkDestroyAccelerationStructureKHR(ctx_.device(), handle_, nullptr); }
    UnbuiltTlas(const UnbuiltTlas&) = delete;
    UnbuiltTlas& operator=(const UnbuiltTlas&) = delete;
    VkAccelerationStructureKHR handle() const { return handle_; }

private:
    Context& ctx_;
    Buffer storage_;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
};

// A 1x1 image cleared to `rgba` (multiples of 1/255) and left ready for sampling in compute shaders, or for
// imageLoad in GENERAL when `storage`.
Image clearedTexture(Context& ctx, VkCommandBuffer cmd, std::array<float, 4> rgba, const char* name,
                     bool storage = false) {
    const VkImageUsageFlags usage =
        (storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT) | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    Image image(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, usage, 1, name});
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue color{};
    std::memcpy(color.float32, rgba.data(), sizeof color.float32);
    vkCmdClearColorImage(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &kColorRange);
    if (storage)
        image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    else
        image.transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return image;
}

using Vec4s = std::array<std::array<float, 4>, 5>; // what the probe writes

const std::array<std::array<float, 4>, 3> kTextureColours = {{
    {1.0f, 0.0f, 51.0f / 255.0f, 1.0f},
    {0.0f, 1.0f, 102.0f / 255.0f, 153.0f / 255.0f},
    {204.0f / 255.0f, 0.0f, 1.0f, 0.0f},
}};
constexpr std::array<float, 4> kInputValue = {0.25f, -2.0f, 1024.5f, 7.0f};
constexpr std::array<float, 4> kStorageColour = {0.0f, 51.0f / 255.0f, 1.0f, 1.0f};

bool near(const Vec4s& got, const Vec4s& want) {
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t c = 0; c < 4; ++c)
            if (std::fabs(got[i][c] - want[i][c]) > 1.0f / 512.0f)
                return false;
    return true;
}

// (a) A layout with every supported type, an acceleration structure and a three-element array among them,
// is created, and a set of it allocated and written with real resources passes the validation layer.
void testLayoutWithEveryType(Context& ctx) {
    const std::array<Binding, 7> bindings = {
        Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        Binding{1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
        Binding{2, VK_DESCRIPTOR_TYPE_SAMPLER},
        Binding{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
        Binding{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        Binding{5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        Binding{6, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
    };
    DescriptorLayout layout(ctx, bindings, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    CHECK(layout.handle() != VK_NULL_HANDLE);
    CHECK(layout.bindings().size() == bindings.size());
    CHECK(layout.bindings()[3].count == 3);

    Image storage(ctx, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "every type storage"});
    Image sampled(ctx, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "every type sampled"});
    Buffer storageBuffer(ctx, {64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "every type storage buffer"});
    Buffer uniformBuffer(ctx, {64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, "every type uniform buffer"});
    UnbuiltTlas tlas(ctx);
    DescriptorAllocator allocator(ctx);
    FrameRing ring(ctx);

    ring.begin();
    const VkDescriptorSet set = allocator.allocate(ring, layout);
    CHECK(set != VK_NULL_HANDLE);
    const VkDescriptorImageInfo combined{ctx.linearSampler(), sampled.view(),
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const std::vector<DescriptorWrite> writes = {
        DescriptorWrite::image(0, {VK_NULL_HANDLE, storage.view(), VK_IMAGE_LAYOUT_GENERAL}),
        DescriptorWrite::image(1, {VK_NULL_HANDLE, sampled.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),
        DescriptorWrite::image(2, {ctx.trilinearSampler(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}),
        DescriptorWrite{3, {combined, combined, combined}, {}, {}},
        DescriptorWrite::buffer(4, {storageBuffer.handle(), 0, VK_WHOLE_SIZE}),
        DescriptorWrite::buffer(5, {uniformBuffer.handle(), 0, VK_WHOLE_SIZE}),
        DescriptorWrite::accelerationStructure(6, tlas.handle()),
    };
    writeDescriptors(ctx, set, layout, writes);
    ring.wait(ring.submit());
    CHECK(allocator.poolCount() == 1);
}

// (b) writeDescriptors refuses a write set that does not match the layout, before writing anything.
void testWritesAreChecked(Context& ctx) {
    DescriptorLayout layout(ctx, kProbeBindings, VK_SHADER_STAGE_COMPUTE_BIT);
    Image texture(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 1, "checked texture"});
    Buffer buffer(ctx, {16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "checked buffer"});
    DescriptorAllocator allocator(ctx);
    FrameRing ring(ctx);

    ring.begin();
    const VkDescriptorSet set = allocator.allocate(ring, layout);
    const VkDescriptorImageInfo image{ctx.linearSampler(), texture.view(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo bufferInfo{buffer.handle(), 0, VK_WHOLE_SIZE};
    const DescriptorWrite storage =
        DescriptorWrite::image(3, {VK_NULL_HANDLE, texture.view(), VK_IMAGE_LAYOUT_GENERAL});
    const std::vector<DescriptorWrite> complete = {DescriptorWrite{0, {image, image, image}, {}, {}},
                                                   DescriptorWrite::buffer(1, bufferInfo),
                                                   DescriptorWrite::buffer(2, bufferInfo), storage};

    // `complete` with write `index` replaced (or dropped, for an empty optional) and `extra` appended.
    auto variant = [&](std::size_t index, const DescriptorWrite* replacement, const DescriptorWrite* extra) {
        std::vector<DescriptorWrite> writes;
        for (std::size_t i = 0; i < complete.size(); ++i)
            if (i != index)
                writes.push_back(complete[i]);
            else if (replacement)
                writes.push_back(*replacement);
        if (extra)
            writes.push_back(*extra);
        return writes;
    };
    auto write = [&](const std::vector<DescriptorWrite>& writes) {
        writeDescriptors(ctx, set, layout, writes);
    };
    std::printf("expecting five refused write sets:\n");
    CHECK(throwsInvalidArgument([&] { write(variant(2, nullptr, nullptr)); })); // binding 2 missing
    const DescriptorWrite again = DescriptorWrite::buffer(1, bufferInfo);
    CHECK(throwsInvalidArgument([&] { write(variant(4, nullptr, &again)); })); // binding 1 twice
    const DescriptorWrite imageForBuffer = DescriptorWrite::image(1, image);
    CHECK(throwsInvalidArgument([&] { write(variant(1, &imageForBuffer, nullptr)); }));
    const DescriptorWrite twoOfThree{0, {image, image}, {}, {}};
    CHECK(throwsInvalidArgument([&] { write(variant(0, &twoOfThree, nullptr)); }));
    const DescriptorWrite notInLayout = DescriptorWrite::buffer(7, bufferInfo);
    CHECK(throwsInvalidArgument([&] { write(variant(4, nullptr, &notInLayout)); }));
    write(complete);
    ring.wait(ring.submit());

    // Layouts refuse a repeated binding number, an empty array and a type outside the supported set.
    const std::array<Binding, 2> twice = {Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                          Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
    CHECK(throwsInvalidArgument([&] { DescriptorLayout(ctx, twice, VK_SHADER_STAGE_COMPUTE_BIT); }));
    const std::array<Binding, 1> empty = {Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0}};
    CHECK(throwsInvalidArgument([&] { DescriptorLayout(ctx, empty, VK_SHADER_STAGE_COMPUTE_BIT); }));
    const std::array<Binding, 1> texel = {Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER}};
    CHECK(throwsInvalidArgument([&] { DescriptorLayout(ctx, texel, VK_SHADER_STAGE_COMPUTE_BIT); }));
}

// (c) The probe reads its three textures through the array and the storage buffer, through both of
// ComputePipeline's binds, and both give the expected values.
void testProbeReadsArrayAndBuffer(Context& ctx) {
    ComputePipeline probe(ctx, kProbe, kProbeBindings, 0);
    Buffer input(ctx, {sizeof kInputValue, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "probe input"});
    std::memcpy(input.mapped(), kInputValue.data(), sizeof kInputValue);
    input.flush();
    std::array<Buffer, 2> outputs = {
        Buffer(ctx, {sizeof(Vec4s), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "out a"}),
        Buffer(ctx, {sizeof(Vec4s), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "out b"})};
    std::vector<Image> textures;
    FrameRing ring(ctx);

    VkCommandBuffer cmd = ring.begin();
    for (std::size_t i = 0; i < kTextureColours.size(); ++i)
        textures.push_back(clearedTexture(ctx, cmd, kTextureColours[i], "probe texture"));
    const Image storageImage = clearedTexture(ctx, cmd, kStorageColour, "probe storage image", true);
    std::vector<VkDescriptorImageInfo> images;
    for (const Image& texture : textures)
        images.push_back({ctx.nearestSampler(), texture.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    const VkDescriptorImageInfo storageInfo{VK_NULL_HANDLE, storageImage.view(), VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo inputInfo{input.handle(), 0, VK_WHOLE_SIZE};

    // The writes overload.
    const VkDescriptorBufferInfo outA{outputs[0].handle(), 0, VK_WHOLE_SIZE};
    const std::array<DescriptorWrite, 4> writes = {
        DescriptorWrite{0, images, {}, {}}, DescriptorWrite::buffer(1, inputInfo),
        DescriptorWrite::buffer(2, outA), DescriptorWrite::image(3, storageInfo)};
    probe.bind(cmd, ring, writes);
    probe.dispatch(cmd, 1, 1);
    // The convenience overload: the array's three images and then the storage image, the buffers in binding
    // order.
    images.push_back(storageInfo);
    const std::array<VkDescriptorBufferInfo, 2> buffers = {
        inputInfo, VkDescriptorBufferInfo{outputs[1].handle(), 0, VK_WHOLE_SIZE}};
    probe.bind(cmd, ring, images, buffers);
    probe.dispatch(cmd, 1, 1);
    VkMemoryBarrier2 toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    toHost.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    toHost.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &toHost;
    vkCmdPipelineBarrier2(cmd, &dependency);

    // The convenience overload still checks its counts: the array takes three images.
    CHECK(throwsInvalidArgument([&] { probe.bind(cmd, ring, std::span(images).first(1), buffers); }));
    ring.wait(ring.submit());

    Vec4s want{};
    for (std::size_t i = 0; i < 3; ++i)
        want[i] = kTextureColours[i];
    want[3] = kInputValue;
    want[4] = kStorageColour;
    std::array<Vec4s, 2> got{};
    for (std::size_t i = 0; i < 2; ++i) {
        outputs[i].invalidate();
        std::memcpy(&got[i], outputs[i].mapped(), sizeof(Vec4s));
    }
    CHECK(near(got[0], want));
    CHECK(std::memcmp(&got[0], &got[1], sizeof(Vec4s)) == 0);
}

// (d) The allocator's pools grow for a frame that needs many sets and are reused, not added to, later.
void testAllocatorReusesPools(Context& ctx) {
    DescriptorLayout layout(ctx, kProbeBindings, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorAllocator allocator(ctx);
    FrameRing ring(ctx);

    bool threw = false;
    try {
        allocator.allocate(ring, layout); // not recording
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);

    auto frame = [&](int sets) {
        ring.begin();
        for (int i = 0; i < sets; ++i)
            CHECK(allocator.allocate(ring, layout) != VK_NULL_HANDLE);
        ring.submit();
    };
    frame(200);
    std::printf("descriptor pools after 200 sets in one frame: %zu\n", allocator.poolCount());
    CHECK(allocator.poolCount() >= 4);
    frame(3);
    frame(200);
    const std::size_t afterThird = allocator.poolCount();
    for (int i = 0; i < 10; ++i)
        frame(i % 2 == 0 ? 3 : 200);
    CHECK(allocator.poolCount() == afterThird);
    ring.waitAll();

    // An allocator belongs to the ring it first allocated with while that ring lives.
    FrameRing other(ctx);
    other.begin();
    threw = false;
    try {
        allocator.allocate(other, layout);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    other.wait(other.submit());
}

// (e) The ray tracing properties come from the driver; on lavapipe they are {32, 16, 32, 31, 8}.
void testRayTracingProperties(Context& ctx) {
    const RayTracingProperties& rt = ctx.rayTracingProperties();
    std::printf("ray tracing properties: handle %u, handle alignment %u, base alignment %u, recursion %u, "
                "scratch alignment %u\n",
                rt.shaderGroupHandleSize, rt.shaderGroupHandleAlignment, rt.shaderGroupBaseAlignment,
                rt.maxRayRecursionDepth, rt.minScratchOffsetAlignment);
    auto powerOfTwo = [](std::uint32_t v) { return v != 0 && (v & (v - 1)) == 0; };
    CHECK(rt.shaderGroupHandleSize >= 16);
    CHECK(powerOfTwo(rt.shaderGroupHandleAlignment) && powerOfTwo(rt.shaderGroupBaseAlignment));
    CHECK(rt.shaderGroupBaseAlignment >= rt.shaderGroupHandleAlignment);
    CHECK(rt.maxRayRecursionDepth >= 1);
    CHECK(powerOfTwo(rt.minScratchOffsetAlignment));
    if (ctx.deviceName().find("llvmpipe") != std::string::npos) {
        CHECK(rt.shaderGroupHandleSize == 32);
        CHECK(rt.shaderGroupHandleAlignment == 16);
        CHECK(rt.shaderGroupBaseAlignment == 32);
        CHECK(rt.minScratchOffsetAlignment == 8);
    }
    const std::string json = ctx.deviceInfoJson();
    std::printf("%s\n", json.c_str());
    CHECK(json.find("\"rayTracing\":true") != std::string::npos);
    CHECK(json.find("\"shaderGroupHandleSize\":" + std::to_string(rt.shaderGroupHandleSize)) !=
          std::string::npos);
    CHECK(json.find("\"shaderGroupBaseAlignment\":" + std::to_string(rt.shaderGroupBaseAlignment)) !=
          std::string::npos);
    CHECK(json.find("\"minScratchOffsetAlignment\":" + std::to_string(rt.minScratchOffsetAlignment)) !=
          std::string::npos);

    CHECK(ctx.trilinearSampler() != VK_NULL_HANDLE);
    CHECK(ctx.trilinearSampler() != ctx.linearSampler() && ctx.trilinearSampler() != ctx.nearestSampler());
}

// (f) A buffer with an alignment starts its device address on it, even between unaligned neighbours.
void testAlignedBuffer(Context& ctx) {
    std::vector<Buffer> buffers;
    for (int i = 0; i < 8; ++i) {
        buffers.emplace_back(
            ctx,
            BufferDesc{100, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       false, "unaligned neighbour"});
        BufferDesc desc{200, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        false, "aligned"};
        desc.alignment = 256;
        buffers.emplace_back(ctx, desc);
        CHECK(buffers.back().address() % 256 == 0);
    }
    BufferDesc wrong{64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "not a power of two"};
    wrong.alignment = 48;
    CHECK(throwsInvalidArgument([&] { Buffer(ctx, wrong); }));
}

// (g) createDeviceBuffer fills a device-local buffer through a staging copy; copied back it equals the input.
void testCreateDeviceBuffer(Context& ctx) {
    std::vector<std::byte> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<std::byte>(i * 13 + 5);
    BufferDesc desc{data.size(),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    false, "device data"};
    desc.alignment = 128;
    Buffer device = createDeviceBuffer(ctx, desc, data);
    CHECK(device.mapped() == nullptr);
    CHECK(device.size() == data.size());
    CHECK(device.address() % 128 == 0);

    Buffer download(ctx, {data.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "download"});
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        const VkBufferCopy region{0, 0, data.size()};
        vkCmdCopyBuffer(cmd, device.handle(), download.handle(), 1, &region);
        bufferBarrier2(cmd, download.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    });
    download.invalidate();
    CHECK(std::memcmp(download.mapped(), data.data(), data.size()) == 0);

    // More data than the buffer holds, or a host-visible desc, is refused.
    BufferDesc small = desc;
    small.size = 10;
    CHECK(throwsInvalidArgument([&] { createDeviceBuffer(ctx, small, data); }));
    BufferDesc host = desc;
    host.hostVisible = true;
    CHECK(throwsInvalidArgument([&] { createDeviceBuffer(ctx, host, data); }));
}

// (h) With sync validation on, a dispatch that reads a storage image another dispatch just wrote is clean
// with the barrier and reported without it. Returns how many messages the hazard raised.
unsigned testSyncValidationReportsMissingBarrier(Context& ctx) {
    const std::array<Binding, 1> fillBindings = {Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};
    ComputePipeline fill(ctx, shaderDir() / "fill.comp.spv", fillBindings, 32);
    ComputePipeline probe(ctx, kProbe, kProbeBindings, 0);
    Image image(ctx, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "hazard image"});
    Buffer input(ctx, {sizeof kInputValue, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "hazard input"});
    Buffer output(ctx, {sizeof(Vec4s), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "hazard output"});
    std::vector<Image> textures;
    FrameRing ring(ctx);
    const std::array<std::uint32_t, 8> push = {4, 4, 0, 0, 0, 0, 0, 0};

    auto frame = [&](bool barrier) {
        VkCommandBuffer cmd = ring.begin();
        if (textures.empty())
            for (std::size_t i = 0; i < kTextureColours.size(); ++i)
                textures.push_back(clearedTexture(ctx, cmd, kTextureColours[i], "hazard texture"));
        image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const VkDescriptorImageInfo storage{VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
        fill.bind(cmd, ring, {&storage, 1}, {});
        fill.push(cmd, push.data(), sizeof push);
        fill.dispatch(cmd, 4, 4);
        if (barrier)
            image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                             VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        std::vector<VkDescriptorImageInfo> images;
        for (const Image& texture : textures)
            images.push_back(
                {ctx.nearestSampler(), texture.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        images.push_back(storage); // binding 3: imageLoad of what fill just wrote
        const std::array<VkDescriptorBufferInfo, 2> buffers = {
            VkDescriptorBufferInfo{input.handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{output.handle(), 0, VK_WHOLE_SIZE}};
        probe.bind(cmd, ring, images, buffers);
        probe.dispatch(cmd, 1, 1);
        // The next frame's fill writes the image again: make it wait for this frame's reads.
        image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        bufferBarrier2(cmd, output.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        ring.wait(ring.submit());
    };

    frame(true);
    CHECK(ctx.validationMessageCount() == 0);
    std::printf("expecting a sync validation hazard (reading a storage image a dispatch just wrote, no "
                "barrier):\n");
    std::fflush(stdout);
    frame(false);
    const unsigned raised = ctx.validationMessageCount();
    CHECK(raised >= 1);
    return raised;
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        {
            auto ctx = Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            CHECK(!ctx.syncValidationEnabled()); // off unless asked for
            testLayoutWithEveryType(ctx);
            testWritesAreChecked(ctx);
            testProbeReadsArrayAndBuffer(ctx);
            testAllocatorReusesPools(ctx);
            testRayTracingProperties(ctx);
            testAlignedBuffer(ctx);
            testCreateDeviceBuffer(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        // Also counts what the layer reported while the objects and the Context were destroyed.
        CHECK(validationMessagesReported() == 0);

        unsigned expected = 0;
        {
            auto ctx = Context::create({.validation = true, .syncValidation = true});
            if (!ctx.syncValidationEnabled()) {
                std::printf(
                    "SKIP sync validation: validation layer or VK_EXT_validation_features unavailable\n");
            } else {
                expected = testSyncValidationReportsMissingBarrier(ctx);
            }
        }
        CHECK(validationMessagesReported() == expected); // exactly the hazard, nothing else
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("descriptors_test: %d checks passed\n", checksPassed);
    return 0;
}
