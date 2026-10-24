#include "gfx/context.hpp"

#include "gfx/vk_check.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace evo::gfx {

namespace {

const char* const kValidationLayer = "VK_LAYER_KHRONOS_validation";

// volk loads instance and device functions into process-wide globals, so a second Context would silently
// repoint the first one's calls at its own device. create() refuses while one is alive.
std::atomic<int> liveContexts{0};

// Counted by debugMessage(); a process-wide count, so main() can still read it after the Context is gone.
std::atomic<unsigned> validationMessages{0};

const std::vector<const char*> kRequiredDeviceExtensions = {
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
};

bool hasInstanceExtension(const char* layer, const char* name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(layer, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(layer, &count, available.data());
    return std::any_of(available.begin(), available.end(), [name](const VkExtensionProperties& p) {
        return std::strcmp(p.extensionName, name) == 0;
    });
}

const char* severityName(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        return "error";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        return "warning";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        return "info";
    return "verbose";
}

// Without a messenger the validation layer prints to stdout, which would corrupt --render-stream output.
VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data,
                                            void* /*user*/) {
    ++validationMessages; // the messenger only subscribes to warnings and errors
    std::fprintf(stderr, "[vulkan] %s: %s\n", severityName(severity),
                 data && data->pMessage ? data->pMessage : "");
    return VK_FALSE; // never abort the call that triggered the message
}

VkDebugUtilsMessengerCreateInfoEXT messengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugMessage;
    return info;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string message = "missing Vulkan requirements:";
    for (const auto& name : names)
        message += " " + name;
    return message;
}

// The feature chain we query and then enable. Linked in the order the brief prescribes.
struct FeatureChain {
    VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    FeatureChain() { link(); }
    FeatureChain(const FeatureChain&) = delete;
    FeatureChain& operator=(const FeatureChain&) = delete;

    void link() {
        core.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = &v13;
        v13.pNext = &accel;
        accel.pNext = &rtPipeline;
        rtPipeline.pNext = &rayQuery;
        rayQuery.pNext = nullptr;
    }

    // Each required flag, as a name plus a pointer into this chain.
    std::vector<std::pair<const char*, VkBool32*>> required() {
        return {
            {"bufferDeviceAddress", &v12.bufferDeviceAddress},
            {"descriptorIndexing", &v12.descriptorIndexing},
            {"runtimeDescriptorArray", &v12.runtimeDescriptorArray},
            {"shaderSampledImageArrayNonUniformIndexing", &v12.shaderSampledImageArrayNonUniformIndexing},
            {"scalarBlockLayout", &v12.scalarBlockLayout},
            {"timelineSemaphore", &v12.timelineSemaphore},
            {"dynamicRendering", &v13.dynamicRendering},
            {"synchronization2", &v13.synchronization2},
            {"maintenance4", &v13.maintenance4},
            {"accelerationStructure", &accel.accelerationStructure},
            {"rayTracingPipeline", &rtPipeline.rayTracingPipeline},
            {"rayQuery", &rayQuery.rayQuery},
        };
    }
};

struct Candidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    uint32_t queueFamily = std::numeric_limits<uint32_t>::max();
    std::vector<std::string> missing;
};

int typeRank(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 1;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 2;
    default:
        return 3;
    }
}

Candidate evaluate(VkPhysicalDevice device, const std::vector<const char*>& extensions) {
    Candidate candidate;
    candidate.device = device;
    vkGetPhysicalDeviceProperties(device, &candidate.properties);

    if (candidate.properties.apiVersion < VK_API_VERSION_1_3)
        candidate.missing.push_back("Vulkan 1.3");

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());
    for (const char* name : extensions) {
        bool found = std::any_of(available.begin(), available.end(), [name](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, name) == 0;
        });
        if (!found)
            candidate.missing.emplace_back(name);
    }

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
    const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if ((families[i].queueFlags & wanted) == wanted) {
            candidate.queueFamily = i;
            break;
        }
    }
    if (candidate.queueFamily == std::numeric_limits<uint32_t>::max())
        candidate.missing.push_back("queue:graphics+compute");

    // Features are only meaningful to query when the extensions that define them exist.
    if (candidate.missing.empty()) {
        FeatureChain chain;
        vkGetPhysicalDeviceFeatures2(device, &chain.core);
        for (auto& [name, flag] : chain.required()) {
            if (!*flag)
                candidate.missing.push_back(std::string("feature:") + name);
        }
    }
    return candidate;
}

std::string versionString(uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
           std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out;
}

} // namespace

MissingRequirements::MissingRequirements(std::vector<std::string> missingNames)
    : std::runtime_error(joinNames(missingNames)), missing(std::move(missingNames)) {}

unsigned validationMessagesReported() {
    return validationMessages.load();
}

Context Context::create(const ContextOptions& options) {
    if (liveContexts.fetch_add(1) != 0) {
        --liveContexts;
        throw std::logic_error("only one evo::gfx::Context may be alive at a time");
    }
    Context ctx;
    ctx.live_ = true; // from here on, ctx's destructor gives the slot back, even if create() throws
    ctx.validationMessagesBefore_ = validationMessages.load();

    if (options.loader)
        volkInitializeCustom(options.loader);
    else if (volkInitialize() != VK_SUCCESS)
        throw MissingRequirements({"Vulkan loader (vulkan-1.dll / libvulkan.so.1)"});

    // --- Instance ---
    std::vector<const char*> layers;
    if (options.validation) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());
        bool present = std::any_of(available.begin(), available.end(), [](const VkLayerProperties& p) {
            return std::strcmp(p.layerName, kValidationLayer) == 0;
        });
        if (present) {
            layers.push_back(kValidationLayer);
            ctx.validation_ = true;
        } else {
            std::fprintf(stderr, "warning: validation layer unavailable\n");
        }
    }

    std::vector<const char*> instanceExtensions = options.instanceExtensions;
    bool useMessenger = false;
    if (options.validation) {
        // The validation layer itself provides VK_EXT_debug_utils, so ask it as well as the loader.
        const char* name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        useMessenger = hasInstanceExtension(nullptr, name) ||
                       (ctx.validation_ && hasInstanceExtension(kValidationLayer, name));
        if (useMessenger) {
            const bool listed =
                std::any_of(instanceExtensions.begin(), instanceExtensions.end(),
                            [name](const char* extension) { return std::strcmp(extension, name) == 0; });
            if (!listed)
                instanceExtensions.push_back(name);
        } else if (ctx.validation_) {
            std::fprintf(stderr, "warning: %s unavailable; validation messages go to stdout\n", name);
        }
    }
    const VkDebugUtilsMessengerCreateInfoEXT debugInfo = messengerInfo();

    // Synchronization validation is switched on through VK_EXT_validation_features, which the layer provides.
    const VkValidationFeatureEnableEXT syncFeature =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validationFeatures.enabledValidationFeatureCount = 1;
    validationFeatures.pEnabledValidationFeatures = &syncFeature;
    if (options.syncValidation && ctx.validation_) {
        const char* name = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
        if (hasInstanceExtension(kValidationLayer, name) || hasInstanceExtension(nullptr, name)) {
            instanceExtensions.push_back(name);
            ctx.syncValidation_ = true;
        } else {
            std::fprintf(stderr, "warning: %s unavailable; no synchronization validation\n", name);
        }
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Evolution";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.pEngineName = "Evolution";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    // Chained here, the messenger also reports problems in vkCreateInstance and vkDestroyInstance.
    instanceInfo.pNext = useMessenger ? &debugInfo : nullptr;
    if (ctx.syncValidation_) {
        validationFeatures.pNext = instanceInfo.pNext;
        instanceInfo.pNext = &validationFeatures;
    }
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    const VkResult instanceResult = vkCreateInstance(&instanceInfo, nullptr, &ctx.instance_);
    if (instanceResult == VK_ERROR_INCOMPATIBLE_DRIVER)
        throw MissingRequirements({"Vulkan 1.3 capable loader/driver"});
    vkCheck(instanceResult, "vkCreateInstance");
    volkLoadInstance(ctx.instance_);
    if (useMessenger) {
        vkCheck(vkCreateDebugUtilsMessengerEXT(ctx.instance_, &debugInfo, nullptr, &ctx.messenger_),
                "vkCreateDebugUtilsMessengerEXT");
    }

    // --- Physical device ---
    std::vector<const char*> extensions = kRequiredDeviceExtensions;
    extensions.insert(extensions.end(), options.extraDeviceExtensions.begin(),
                      options.extraDeviceExtensions.end());
    // Test-only hook (see context.hpp); copied because a later setenv may free getenv's string.
    std::string extensionRequiredByTest;
    if (const char* name = std::getenv("EVO_TEST_REQUIRE_DEVICE_EXTENSION"); name && *name) {
        extensionRequiredByTest = name;
        extensions.push_back(extensionRequiredByTest.c_str());
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx.instance_, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(ctx.instance_, &deviceCount, devices.data());
    if (devices.empty())
        throw MissingRequirements({"a Vulkan physical device"});

    std::vector<Candidate> candidates;
    for (VkPhysicalDevice device : devices)
        candidates.push_back(evaluate(device, extensions));

    const Candidate* chosen = nullptr;
    for (const auto& candidate : candidates) {
        if (!candidate.missing.empty())
            continue;
        if (!chosen || typeRank(candidate.properties.deviceType) < typeRank(chosen->properties.deviceType))
            chosen = &candidate;
    }
    if (!chosen) {
        const Candidate* best = &candidates.front();
        for (const auto& candidate : candidates) {
            if (candidate.missing.size() < best->missing.size())
                best = &candidate;
        }
        throw MissingRequirements(best->missing);
    }
    ctx.physicalDevice_ = chosen->device;
    ctx.queueFamily_ = chosen->queueFamily;

    // --- Ray tracing properties, as the driver reports them ---
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    rtProperties.pNext = &accelProperties;
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &rtProperties;
    vkGetPhysicalDeviceProperties2(ctx.physicalDevice_, &properties2);
    ctx.rayTracing_ = {rtProperties.shaderGroupHandleSize,
                       rtProperties.shaderGroupHandleAlignment,
                       rtProperties.shaderGroupBaseAlignment,
                       rtProperties.maxRayRecursionDepth,
                       accelProperties.minAccelerationStructureScratchOffsetAlignment,
                       rtProperties.maxShaderGroupStride};

    // --- Logical device: enable exactly the required features ---
    FeatureChain enabled;
    for (auto& [name, flag] : enabled.required())
        *flag = VK_TRUE;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = ctx.queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &enabled.core;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.data();
    vkCheck(vkCreateDevice(ctx.physicalDevice_, &deviceInfo, nullptr, &ctx.device_), "vkCreateDevice");
    volkLoadDevice(ctx.device_);
    vkGetDeviceQueue(ctx.device_, ctx.queueFamily_, 0, &ctx.queue_);

    // --- Allocator ---
    VmaVulkanFunctions functions{};
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = ctx.physicalDevice_;
    allocatorInfo.device = ctx.device_;
    allocatorInfo.instance = ctx.instance_;
    vkCheck(vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &functions), "vmaImportVulkanFunctionsFromVolk");
    allocatorInfo.pVulkanFunctions = &functions;
    vkCheck(vmaCreateAllocator(&allocatorInfo, &ctx.allocator_), "vmaCreateAllocator");

    // --- Command pool ---
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx.queueFamily_;
    vkCheck(vkCreateCommandPool(ctx.device_, &poolInfo, nullptr, &ctx.commandPool_), "vkCreateCommandPool");

    // --- Samplers ---
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    vkCheck(vkCreateSampler(ctx.device_, &samplerInfo, nullptr, &ctx.nearestSampler_),
            "vkCreateSampler(nearest)");
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    vkCheck(vkCreateSampler(ctx.device_, &samplerInfo, nullptr, &ctx.linearSampler_),
            "vkCreateSampler(linear)");
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    vkCheck(vkCreateSampler(ctx.device_, &samplerInfo, nullptr, &ctx.trilinearSampler_),
            "vkCreateSampler(trilinear)");
    ctx.setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(ctx.nearestSampler_),
                      "nearest");
    ctx.setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(ctx.linearSampler_), "linear");
    ctx.setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(ctx.trilinearSampler_),
                      "trilinear");

    return ctx;
}

Context::~Context() {
    destroy();
}

Context::Context(Context&& other) noexcept
    : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
      messenger_(std::exchange(other.messenger_, VK_NULL_HANDLE)),
      physicalDevice_(std::exchange(other.physicalDevice_, VK_NULL_HANDLE)),
      device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      queue_(std::exchange(other.queue_, VK_NULL_HANDLE)), queueFamily_(other.queueFamily_),
      allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
      commandPool_(std::exchange(other.commandPool_, VK_NULL_HANDLE)),
      nearestSampler_(std::exchange(other.nearestSampler_, VK_NULL_HANDLE)),
      linearSampler_(std::exchange(other.linearSampler_, VK_NULL_HANDLE)),
      trilinearSampler_(std::exchange(other.trilinearSampler_, VK_NULL_HANDLE)),
      rayTracing_(other.rayTracing_), validation_(other.validation_), syncValidation_(other.syncValidation_),
      validationMessagesBefore_(other.validationMessagesBefore_), live_(std::exchange(other.live_, false)) {}

Context& Context::operator=(Context&& other) noexcept {
    if (this != &other) {
        destroy();
        instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
        messenger_ = std::exchange(other.messenger_, VK_NULL_HANDLE);
        physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        queue_ = std::exchange(other.queue_, VK_NULL_HANDLE);
        queueFamily_ = other.queueFamily_;
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
        commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
        nearestSampler_ = std::exchange(other.nearestSampler_, VK_NULL_HANDLE);
        linearSampler_ = std::exchange(other.linearSampler_, VK_NULL_HANDLE);
        trilinearSampler_ = std::exchange(other.trilinearSampler_, VK_NULL_HANDLE);
        rayTracing_ = other.rayTracing_;
        validation_ = other.validation_;
        syncValidation_ = other.syncValidation_;
        validationMessagesBefore_ = other.validationMessagesBefore_;
        live_ = std::exchange(other.live_, false);
    }
    return *this;
}

void Context::destroy() noexcept {
    if (device_) {
        vkDeviceWaitIdle(device_);
        if (nearestSampler_)
            vkDestroySampler(device_, nearestSampler_, nullptr);
        if (linearSampler_)
            vkDestroySampler(device_, linearSampler_, nullptr);
        if (trilinearSampler_)
            vkDestroySampler(device_, trilinearSampler_, nullptr);
        if (commandPool_)
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        if (allocator_)
            vmaDestroyAllocator(allocator_);
        vkDestroyDevice(device_, nullptr);
    }
    if (messenger_)
        vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    messenger_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    nearestSampler_ = VK_NULL_HANDLE;
    linearSampler_ = VK_NULL_HANDLE;
    trilinearSampler_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    if (live_) {
        live_ = false;
        --liveContexts;
    }
}

unsigned Context::validationMessageCount() const {
    return validationMessages.load() - validationMessagesBefore_;
}

std::string Context::deviceName() const {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    return properties.deviceName;
}

uint32_t Context::apiVersion() const {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    return properties.apiVersion;
}

std::string Context::deviceInfoJson() const {
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physicalDevice_, &properties);

    // A context only exists when every ray-tracing requirement was met, so rayTracing is always true.
    return std::string("{\"device\":\"") + jsonEscape(properties.properties.deviceName) + "\",\"driver\":\"" +
           jsonEscape(std::string(driver.driverName) + " " + driver.driverInfo) + "\",\"api\":\"" +
           versionString(properties.properties.apiVersion) +
           "\",\"rayTracing\":true,\"rayTracingProperties\":{\"shaderGroupHandleSize\":" +
           std::to_string(rayTracing_.shaderGroupHandleSize) +
           ",\"shaderGroupHandleAlignment\":" + std::to_string(rayTracing_.shaderGroupHandleAlignment) +
           ",\"shaderGroupBaseAlignment\":" + std::to_string(rayTracing_.shaderGroupBaseAlignment) +
           ",\"maxRayRecursionDepth\":" + std::to_string(rayTracing_.maxRayRecursionDepth) +
           ",\"minScratchOffsetAlignment\":" + std::to_string(rayTracing_.minScratchOffsetAlignment) +
           ",\"maxShaderGroupStride\":" + std::to_string(rayTracing_.maxShaderGroupStride) +
           "},\"validation\":" + (validation_ ? "true" : "false") + "}";
}

void Context::setObjectName(VkObjectType type, std::uint64_t handle, const char* name) const {
    // The messenger exists exactly when VK_EXT_debug_utils was enabled on the instance.
    if (!messenger_ || !vkSetDebugUtilsObjectNameEXT || !name || !*name || handle == 0)
        return;
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

void Context::immediate(const std::function<void(VkCommandBuffer)>& record) const {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkCheck(vkAllocateCommandBuffers(device_, &allocInfo, &cmd), "vkAllocateCommandBuffers");

    try {
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");
        record(cmd);
        vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

        VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmdInfo.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        vkCheck(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");
        vkCheck(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
    } catch (...) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        throw;
    }
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
}

} // namespace evo::gfx
