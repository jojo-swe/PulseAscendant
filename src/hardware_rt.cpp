#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hardware_rt.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace remix {
namespace {
void check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan RT error "+std::to_string(r));}
struct V3 {float x,y,z;};
V3 operator+(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V3 operator-(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V3 operator*(V3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V3 norm(V3 a){return a*(1.f/std::sqrt(std::max(1e-12f,dot(a,a))));}
constexpr unsigned maxInstances=2048;
constexpr float pi=3.14159265359f;
}
#define RT_INSTANCE(X) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define RT_DEVICE(X) X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkGetBufferDeviceAddress) X(vkCreateAccelerationStructureKHR) X(vkDestroyAccelerationStructureKHR) X(vkGetAccelerationStructureBuildSizesKHR) X(vkGetAccelerationStructureDeviceAddressKHR) X(vkCmdBuildAccelerationStructuresKHR) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkQueueSubmit) X(vkWaitForFences) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch) X(vkCmdCopyBuffer)
struct HardwareRT::Impl {
 SDL_SharedObject* library=nullptr;
 PFN_vkGetInstanceProcAddr get=nullptr;
#define DECL(N) PFN_##N N=nullptr;
 RT_INSTANCE(DECL) RT_DEVICE(DECL)
#undef DECL
 VkInstance instance=VK_NULL_HANDLE;
 VkPhysicalDevice physical=VK_NULL_HANDLE;
 VkDevice device=VK_NULL_HANDLE;
 VkQueue queue=VK_NULL_HANDLE;
 VkCommandPool commandPool=VK_NULL_HANDLE;
 VkCommandBuffer cmd=VK_NULL_HANDLE;
 VkFence fence=VK_NULL_HANDLE;
 VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
 VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
 VkDescriptorSet set=VK_NULL_HANDLE;
 VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
 VkPipeline pipeline=VK_NULL_HANDLE;
 VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
 PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger=nullptr;
 VkPhysicalDeviceMemoryProperties memory{};
 std::string name;
 std::atomic<unsigned> validationErrors{0};
 uint32_t queueFamily=0,scratchAlignment=256;
 uint64_t maxOutputBytes=0;
 int width=0,height=0;
 uint64_t frames=0;
 struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkDeviceAddress address=0;void* mapped=nullptr;VkDeviceSize size=0;};
 std::vector<std::unique_ptr<Buffer>> buffers;
 struct Acceleration {VkAccelerationStructureKHR handle=VK_NULL_HANDLE;VkDeviceAddress address=0;};
 std::array<Acceleration,5> meshes{};
 Acceleration tlas{};
 Buffer *instanceBuffer=nullptr,*objects=nullptr,*parameters=nullptr,*output=nullptr,*scratch=nullptr,*readback=nullptr,*objectUpload=nullptr,*parameterUpload=nullptr;
 struct Object {float base[4],emission[4],material[4];};
 struct Parameters {float eye[4],forward[4],right[4],up[4],clock[4],music[4],bands[24];};
 std::vector<VkAccelerationStructureInstanceKHR> instances;
 std::vector<Object> objectData;
 ~Impl(){
  if(device){
   if(vkDeviceWaitIdle)vkDeviceWaitIdle(device);
   if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
   if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
   if(descriptorPool)vkDestroyDescriptorPool(device,descriptorPool,nullptr);
   if(setLayout)vkDestroyDescriptorSetLayout(device,setLayout,nullptr);
   if(fence)vkDestroyFence(device,fence,nullptr);
   if(commandPool)vkDestroyCommandPool(device,commandPool,nullptr);
   if(tlas.handle)vkDestroyAccelerationStructureKHR(device,tlas.handle,nullptr);
   for(auto& mesh:meshes)if(mesh.handle)vkDestroyAccelerationStructureKHR(device,mesh.handle,nullptr);
   for(auto& b:buffers)release(*b);
   if(vkDestroyDevice)vkDestroyDevice(device,nullptr);
  }
  if(messenger&&destroyMessenger)destroyMessenger(instance,messenger,nullptr);
  if(instance&&vkDestroyInstance)vkDestroyInstance(instance,nullptr);
  if(library)SDL_UnloadObject(library);
 }
 static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user){
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++static_cast<Impl*>(user)->validationErrors;
  std::cerr<<"VULKAN VALIDATION: "<<data->pMessage<<'\n';return VK_FALSE;
 }
 void release(Buffer& b){if(b.mapped)vkUnmapMemory(device,b.memory);if(b.buffer)vkDestroyBuffer(device,b.buffer,nullptr);if(b.memory)vkFreeMemory(device,b.memory,nullptr);b={};}
 Buffer* buffer(VkDeviceSize bytes,VkBufferUsageFlags usage,bool host=false){
  buffers.push_back(std::make_unique<Buffer>());auto* b=buffers.back().get();b->size=bytes;
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};create.size=bytes;create.usage=usage;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device,&create,nullptr,&b->buffer));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,b->buffer,&req);
  VkMemoryPropertyFlags wanted=host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  unsigned type=memory.memoryTypeCount;int bestMemory=-1;
  for(unsigned i=0;i<memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&wanted)==wanted){
   // CPU reads of uncached/write-combined staging memory can dominate the entire frame.
   int score=host&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?10:0;
   if(score>bestMemory){type=i;bestMemory=score;}
  }
  if(type==memory.memoryTypeCount)throw std::runtime_error("No compatible Vulkan memory type");
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=req.size;allocate.memoryTypeIndex=type;
  if(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)allocate.pNext=&flags;
  check(vkAllocateMemory(device,&allocate,nullptr,&b->memory));check(vkBindBufferMemory(device,b->buffer,b->memory,0));
  if(host)check(vkMapMemory(device,b->memory,0,VK_WHOLE_SIZE,0,&b->mapped));
  if(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT){VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};info.buffer=b->buffer;b->address=vkGetBufferDeviceAddress(device,&info);}
  return b;
 }
 void begin(){check(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(cmd,&info));}
 void submit(){check(vkEndCommandBuffer(cmd));check(vkResetFences(device,1,&fence));VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&cmd;check(vkQueueSubmit(queue,1,&info,fence));check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));if(validationErrors)throw std::runtime_error("Vulkan validation reported errors");}
 void barrier(VkAccessFlags from,VkAccessFlags to,VkPipelineStageFlags src,VkPipelineStageFlags dst){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=from;b.dstAccessMask=to;vkCmdPipelineBarrier(cmd,src,dst,0,1,&b,0,nullptr,0,nullptr);}
 VkDeviceAddress scratchAddress(Buffer* b){return (b->address+scratchAlignment-1)&~VkDeviceAddress(scratchAlignment-1);}
 Acceleration acceleration(VkAccelerationStructureTypeKHR type,VkDeviceSize size){
  auto* b=buffer(size,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};info.buffer=b->buffer;info.size=size;info.type=type;Acceleration result;
  check(vkCreateAccelerationStructureKHR(device,&info,nullptr,&result.handle));
  VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};address.accelerationStructure=result.handle;result.address=vkGetAccelerationStructureDeviceAddressKHR(device,&address);return result;
 }
 void mesh(unsigned kind,const std::vector<V3>& vertices,const std::vector<uint32_t>& indices){
  const auto usage=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  auto* v=buffer(vertices.size()*sizeof(V3),usage,true);std::memcpy(v->mapped,vertices.data(),size_t(v->size));
  auto* i=buffer(indices.size()*4,usage,true);std::memcpy(i->mapped,indices.data(),size_t(i->size));
  VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
  auto& tri=geometry.geometry.triangles;tri.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;tri.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;tri.vertexData.deviceAddress=v->address;tri.vertexStride=sizeof(V3);tri.maxVertex=uint32_t(vertices.size()-1);tri.indexType=VK_INDEX_TYPE_UINT32;tri.indexData.deviceAddress=i->address;
  VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;
  uint32_t count=uint32_t(indices.size()/3);VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};vkGetAccelerationStructureBuildSizesKHR(device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);
  meshes[kind]=acceleration(build.type,sizes.accelerationStructureSize);auto* temp=buffer(sizes.buildScratchSize+scratchAlignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);build.dstAccelerationStructure=meshes[kind].handle;build.scratchData.deviceAddress=scratchAddress(temp);
  VkAccelerationStructureBuildRangeInfoKHR range{};range.primitiveCount=count;const auto* ranges=&range;
  begin();vkCmdBuildAccelerationStructuresKHR(cmd,1,&build,&ranges);barrier(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);submit();release(*temp);
 }
 VkAccelerationStructureGeometryKHR instanceGeometry(){VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};g.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;g.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;g.geometry.instances.data.deviceAddress=instanceBuffer->address;return g;}
 void initialize(const std::filesystem::path& shader,bool validation){
#ifdef _WIN32
  library=SDL_LoadObject("vulkan-1.dll");
#else
  library=SDL_LoadObject("libvulkan.so.1");
#endif
  if(!library)throw std::runtime_error("Vulkan loader unavailable");get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library,"vkGetInstanceProcAddr"));if(!get)throw std::runtime_error("Vulkan entry point unavailable");
  auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Pulse Ascendant / Vulkan RT";app.apiVersion=VK_API_VERSION_1_2;
  const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&app;
  if(validation){create.enabledLayerCount=1;create.ppEnabledLayerNames=&layer;create.enabledExtensionCount=1;create.ppEnabledExtensionNames=&extension;}
  check(createInstance(&create,nullptr,&instance));
#define LOAD_INSTANCE(N) N=reinterpret_cast<PFN_##N>(get(instance,#N));if(!N)throw std::runtime_error("Missing Vulkan " #N);
  RT_INSTANCE(LOAD_INSTANCE)
#undef LOAD_INSTANCE
  if(validation){auto make=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(instance,"vkCreateDebugUtilsMessengerEXT"));destroyMessenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(get(instance,"vkDestroyDebugUtilsMessengerEXT"));VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};debugInfo.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debugInfo.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debugInfo.pfnUserCallback=debug;debugInfo.pUserData=this;check(make(instance,&debugInfo,nullptr,&messenger));}
  uint32_t n=0;check(vkEnumeratePhysicalDevices(instance,&n,nullptr));std::vector<VkPhysicalDevice> devices(n);check(vkEnumeratePhysicalDevices(instance,&n,devices.data()));int best=-1;
  const char* required[]={VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  for(auto candidate:devices){
   VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(candidate,&props);if(props.apiVersion<VK_API_VERSION_1_2)continue;
   // Mesa software devices may expose ray queries too; they are not hardware RT.
   if(props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU&&props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU&&props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)continue;
   uint32_t count=0;check(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&count,nullptr));std::vector<VkExtensionProperties> extensions(count);check(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&count,extensions.data()));
   bool supported=true;for(auto ext:required)supported&=std::any_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(ext,e.extensionName)==0;});if(!supported)continue;
   VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};bda.pNext=&as;as.pNext=&query;VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.pNext=&bda;vkGetPhysicalDeviceFeatures2(candidate,&f);if(!query.rayQuery||!as.accelerationStructure||!bda.bufferDeviceAddress)continue;
   vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,nullptr);std::vector<VkQueueFamilyProperties> queues(count);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,queues.data());
   for(uint32_t q=0;q<count;++q)if(queues[q].queueCount&&(queues[q].queueFlags&VK_QUEUE_COMPUTE_BIT)){int score=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?100:50;if(score>best){best=score;physical=candidate;queueFamily=q;name=props.deviceName;}break;}
  }
  if(!physical)throw std::runtime_error("No device with KHR acceleration structures and ray queries");
  float priority=1;VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queueInfo.queueFamilyIndex=queueFamily;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
  VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};query.rayQuery=VK_TRUE;VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};as.accelerationStructure=VK_TRUE;as.pNext=&query;VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};bda.bufferDeviceAddress=VK_TRUE;bda.pNext=&as;
  VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&bda;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&queueInfo;dc.enabledExtensionCount=3;dc.ppEnabledExtensionNames=required;check(vkCreateDevice(physical,&dc,nullptr,&device));
#define LOAD_DEVICE(N) N=reinterpret_cast<PFN_##N>(vkGetDeviceProcAddr(device,#N));if(!N)throw std::runtime_error("Missing Vulkan " #N);
  RT_DEVICE(LOAD_DEVICE)
#undef LOAD_DEVICE
  vkGetDeviceQueue(device,queueFamily,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
  VkPhysicalDeviceAccelerationStructurePropertiesKHR ap{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&ap;vkGetPhysicalDeviceProperties2(physical,&props);scratchAlignment=ap.minAccelerationStructureScratchOffsetAlignment;maxOutputBytes=props.properties.limits.maxStorageBufferRange;
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=queueFamily;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;check(vkCreateCommandPool(device,&pool,nullptr,&commandPool));VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=commandPool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;check(vkAllocateCommandBuffers(device,&ca,&cmd));VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fi,nullptr,&fence));
  mesh(0,{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},{0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,2,3,7,2,7,6,1,2,6,1,6,5,3,0,4,3,4,7});
  std::vector<V3> sv;std::vector<uint32_t> si;for(int y=0;y<=16;++y)for(int x=0;x<=32;++x){float a=x*2*pi/32,b=y*pi/16;sv.push_back({std::sin(b)*std::cos(a),std::cos(b),std::sin(b)*std::sin(a)});}for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){uint32_t a=y*33+x,b=a+33;si.insert(si.end(),{a,b,a+1,a+1,b,b+1});}mesh(1,sv,si);
  mesh(2,{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}},{0,2,4,4,2,1,1,2,5,5,2,0,0,4,3,4,1,3,1,5,3,5,0,3});
  sv.clear();si.clear();for(int y=0;y<=12;++y)for(int x=0;x<=64;++x){float a=x*2*pi/64,b=y*2*pi/12,r=1+.045f*std::cos(b);sv.push_back({r*std::cos(a),r*std::sin(a),.045f*std::sin(b)});}for(unsigned y=0;y<12;++y)for(unsigned x=0;x<64;++x){uint32_t a=y*65+x,b=a+65;si.insert(si.end(),{a,b,a+1,a+1,b,b+1});}mesh(3,sv,si);
  sv.clear();si.clear();for(int z=0;z<=64;++z)for(int x=0;x<=64;++x){float px=(x-32)*2.5f,pz=(z-32)*2.5f;sv.push_back({px,-1.8f+.32f*std::sin(px*.13f+pz*.035f)+.22f*std::sin(pz*.19f-px*.04f),pz});}
  for(unsigned z=0;z<64;++z)for(unsigned x=0;x<64;++x){uint32_t a=z*65+x,b=a+65;si.insert(si.end(),{a,a+1,b,a+1,b+1,b});}mesh(4,sv,si);
  instanceBuffer=buffer(maxInstances*sizeof(VkAccelerationStructureInstanceKHR),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,true);objects=buffer(maxInstances*sizeof(Object),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  parameters=buffer(sizeof(Parameters),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  objectUpload=buffer(maxInstances*sizeof(Object),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
  parameterUpload=buffer(sizeof(Parameters),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
  auto geometry=instanceGeometry();VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.geometryCount=1;build.pGeometries=&geometry;uint32_t count=maxInstances;VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};vkGetAccelerationStructureBuildSizesKHR(device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&count,&sizes);tlas=acceleration(build.type,sizes.accelerationStructureSize);scratch=buffer(sizes.buildScratchSize+scratchAlignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  std::array<VkDescriptorSetLayoutBinding,4> bindings{};const VkDescriptorType types[]={VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};for(unsigned i=0;i<4;++i){bindings[i].binding=i;bindings[i].descriptorType=types[i];bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
  VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=4;sl.pBindings=bindings.data();check(vkCreateDescriptorSetLayout(device,&sl,nullptr,&setLayout));VkDescriptorPoolSize ps[]={{types[0],1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=3;dp.pPoolSizes=ps;check(vkCreateDescriptorPool(device,&dp,nullptr,&descriptorPool));VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=descriptorPool;da.descriptorSetCount=1;da.pSetLayouts=&setLayout;check(vkAllocateDescriptorSets(device,&da,&set));
  VkWriteDescriptorSetAccelerationStructureKHR aw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};aw.accelerationStructureCount=1;aw.pAccelerationStructures=&tlas.handle;VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.pNext=&aw;write.dstSet=set;write.dstBinding=0;write.descriptorCount=1;write.descriptorType=types[0];vkUpdateDescriptorSets(device,1,&write,0,nullptr);descriptor(2,objects,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);descriptor(3,parameters,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  std::ifstream file(shader,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing Vulkan RT SPIR-V");auto bytes=file.tellg();if(bytes<=0||bytes%4)throw std::runtime_error("Invalid SPIR-V size");std::vector<uint32_t> code(size_t(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);if(!file)throw std::runtime_error("Truncated SPIR-V");VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=size_t(bytes);sm.pCode=code.data();VkShaderModule module=VK_NULL_HANDLE;check(vkCreateShaderModule(device,&sm,nullptr,&module));
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&setLayout;check(vkCreatePipelineLayout(device,&pl,nullptr,&pipelineLayout));VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=pipelineLayout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline);vkDestroyShaderModule(device,module,nullptr);check(result);
 }
 void descriptor(unsigned binding,Buffer* b,VkDescriptorType type){VkDescriptorBufferInfo info{b->buffer,0,b->size};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=type;w.pBufferInfo=&info;vkUpdateDescriptorSets(device,1,&w,0,nullptr);}
 void add(unsigned kind,V3 p,V3 scale,float yaw,float roll,V3 color,float metal,float rough,V3 emission={},float glass=0,float pitch=0){
  if(instances.size()>=maxInstances)throw std::runtime_error("RT instance budget exceeded");
  float c=std::cos(yaw),s=std::sin(yaw),a=std::cos(roll),b=std::sin(roll);
  float cp=std::cos(pitch),sp=std::sin(pitch);
  VkAccelerationStructureInstanceKHR instance{};instance.transform={{{c*a*scale.x,(-c*b*cp+s*sp)*scale.y,(s*cp+c*b*sp)*scale.z,p.x},{b*scale.x,a*cp*scale.y,-a*sp*scale.z,p.y},{-s*a*scale.x,(s*b*cp+c*sp)*scale.y,(c*cp-s*b*sp)*scale.z,p.z}}};instance.instanceCustomIndex=uint32_t(instances.size());instance.mask=(emission.x+emission.y+emission.z>1)?2:1;instance.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;instance.accelerationStructureReference=meshes[kind].address;instances.push_back(instance);
  objectData.push_back({{color.x,color.y,color.z,metal},{emission.x,emission.y,emission.z,0},{rough,float(kind),glass,1.45f}});
 }
 #include "act3_rt.inl"
 Parameters scene(const TimelineFrame& f,int w,int h){
  instances.clear();objectData.clear();float t=float(f.motionSeconds),bass=f.bass;int world=f.scene-8;
  V3 cyan{.035f,.55f,.8f},gold{.95f,.72f,.25f},pink{.8f,.035f,.22f},steel{.65f,.73f,.85f};
  // Each occurrence is a directed set piece. The music accent advances an
  // irreversible action; the chapter clock never cycles the camera or set.
  const int variant=f.variant;
  const float p=f.chapterProgress,action=f.action,impact=f.event;
  auto ease=[](float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);};
  auto mix=[](V3 a,V3 b,float x){return a*(1-x)+b*x;};
  auto beam=[&](V3 from,V3 to,float radius,V3 base,float metal,float rough,V3 emission=V3{}){
   V3 delta=to-from;float length=std::sqrt(dot(delta,delta));
   if(length<.002f)return;
   add(0,(from+to)*.5f,{radius,radius,length*.5f},std::atan2(delta.x,delta.z),0,base,metal,rough,emission,0,-std::asin(std::clamp(delta.y/length,-1.f,1.f)));
  };
  auto floor=[&](float extent,float rough=.1f){add(0,{0,-1.4f,0},{extent,.3f,extent},0,0,{.25f,.3f,.38f},.85f,rough);};
  V3 eye{},target{0,2,0};
  if(world==0){
   floor(65);
   if(variant==0){
    // A flat slab tears upward into a chrome spine, one blade after another.
    for(int j=0;j<28;++j){float z=(j-13.5f)*.72f,grow=ease(p*2.4f-j*.035f),height=.12f+grow*(2.8f+1.4f*std::sin(j*.21f));
     add(0,{std::sin(j*.3f)*1.2f,height-1,z},{2.7f,height,.16f},j*.025f,(-.3f+.6f*grow)*std::sin(j*.27f),steel,.98f,.08f);
     beam({-2.7f,height*2-1.02f,z},{2.7f,height*2-1.02f,z},.035f,cyan,0,.2f,cyan*(1.2f+f.spectrum[j%24]));
    }
    eye=mix({16,10,-29},{-16,12,-25},p);target=mix({0,1,0},{0,3,0},p);
   }else if(variant==1){
    // A monumental vaulted gallery unfolds overhead; the central aisle stays open.
    for(int j=0;j<12;++j){float z=(j-5.5f)*4.6f,open=ease(action+j*.025f),height=9+open*5;
     for(int side:{-1,1}){
      V3 foot{side*8.f,-1,z},elbow{side*(7.f+open*1.5f),height*.62f,z},apex{side*(.25f+open),height,z};
      beam(foot,elbow,.23f,steel,.97f,.11f);beam(elbow,apex,.23f,steel,.97f,.1f);
      beam(elbow+V3{side*-.3f,0,-.25f},apex+V3{side*-.3f,0,-.25f},.045f,gold,0,.2f,gold*(1.4f+impact));
      add(0,{side*9.1f,3,z},{.18f,4,1.6f},0,side*-.08f,steel,.95f,.08f);
     }
    }
    eye={-1.7f+3.4f*p,1.8f+4.5f*p,-24+32*p};target=eye+V3{0,2,13};
   }else if(variant==2){
    // Four flowing metal ribbons resolve from a liquid sheet into a rising wave.
    for(int lane=0;lane<4;++lane)for(int j=0;j<56;++j){float z=(j-27.5f)*.52f,phase=z*.3f+float(f.localSeconds)*1.1f+lane*.7f;
     float height=1.3f+(1+action*2.4f)*std::sin(phase),x=(lane-1.5f)*2.4f+std::sin(phase*.7f)*action*1.5f;
     add(0,{x,height,z},{.9f,.065f,.3f},.06f*std::cos(phase),.35f*std::cos(phase),lane%2?gold:steel,.99f,.055f);
     if(j%7==0)beam({x-.85f,height+.08f,z},{x+.85f,height+.08f,z},.025f,cyan,0,.2f,cyan*(1.3f+bass));
    }
    eye=mix({15,5,-22},{9,9,-9},p);target={0,2,1};
   }else if(variant==3){
    // A staggered gold skyline is extruded from the mirror floor as we ascend.
    for(int x=-4;x<=4;++x)for(int z=-3;z<=3;++z){float seed=.5f+.5f*std::sin(float(x*13+z*7)),grow=ease(p*2.f-.28f*seed),height=.15f+grow*(3+15*seed);
     add(0,{x*2.4f,height-1,z*2.4f},{.7f,height,.7f},.1f*x,0,gold,.95f,.12f);
     add(0,{x*2.4f,height*2-.94f,z*2.4f},{.72f,.035f,.72f},.1f*x,0,cyan,0,.2f,cyan*(1.4f+bass));
    }
    eye=mix({27,12,-38},{34,46,-48},p);target=mix({0,6,0},{0,16,0},p);
   }else if(variant==4){
    // A single mirror book fans into a long, asymmetrical staircase of light.
    for(int j=0;j<36;++j){float spread=ease(action+.015f*j),a=(-.65f+j*.038f)*spread;
     V3 base{(j-17.5f)*.35f*spread,1+j*.07f*spread,(j-17.5f)*.9f*spread};
     add(0,base,{4.5f,.08f,2.4f},a,-.6f*spread+j*.025f,steel,.98f,.065f);
     beam(base+V3{-4.5f,.15f,0},base+V3{4.5f,.15f,0},.045f,j%3?cyan:gold,0,.2f,(j%3?cyan:gold)*(1.5f+f.spectrum[j%24]*2));
    }
    eye=mix({-22,11,-28},{-24,16,-38},p);target={0,2.8f,0};
   }else{
    // The final cathedral is a pair of massive folded doors, then a clear flight out.
    float open=action;
    for(int side:{-1,1})for(int j=0;j<18;++j){float z=(j-8.5f)*1.3f,x=side*(3.1f+open*(6+j*.08f));
     add(0,{x,6,z},{2.8f,7,.32f},side*open*.55f,side*open*.18f,steel,.98f,.07f);
     add(0,{x-side*2.8f,6,z-.36f},{.06f,6.9f,.04f},0,side*open*.18f,gold,0,.2f,gold*(1.5f+impact*2));
    }
    for(int side:{-1,1})beam({side*9.f,-1,-18},{side*9.f,-1,35},.08f,cyan,0,.2f,cyan*(2+bass));
    add(2,{0,8,36},{2.4f,7,2.4f},.3f,0,gold,.92f,.1f);
    eye={18*(1-ease(p*1.6f)),10-5*p,-34+42*p};target={0,5,6+30*p};
   }
  }else if(world==1){
   floor(100,.07f);
   const float local=float(f.localSeconds);
   // The city is rebuilt for each visit: avenue, rail chase, folded circuit,
   // aerial canyon, then an evacuation into the horizon.
   for(int side:{-1,1})for(int j=0;j<22;++j){float seed=.5f+.5f*std::sin(j*7.1f+side),z=(j-10.5f)*7.f,height=4+seed*(variant==3?24:14);
    float x=side*(variant==2?13.f:11.f),lean=variant==2?side*action*.2f:0;
    if(variant==3)x=side*(16.f+(j%3)*4);
    if(variant==4)x=side*(13.f+action*(j%4)*3.f);
    add(0,{x,height-1,z},{2.9f,height,2.6f},variant==4?side*action*.12f:0,lean,{.07f,.12f,.2f},.65f,.23f);
    for(int level=0;level<4;++level){V3 e=(j+level)%3?cyan:pink;float y=height*(.3f+level*.43f);
     add(0,{x-side*2.94f,y,z},{.035f,.1f,2.4f},0,lean,e,0,.2f,e*(.7f+f.spectrum[(j+level*5)%24]*2));
    }
   }
   for(int side:{-1,1})for(int j=0;j<30;++j){float z=(j-14.5f)*5;
    add(0,{side*4.8f,-1.075f,z},{.055f,.015f,1.8f},variant==2?side*.12f:0,0,cyan,0,.2f,cyan*(1.4f+bass));
   }
   if(variant==0){
    // One cross-street convoy passes, revealing the rain-lit avenue behind it.
    beam({-23,8,14},{23,8,14},.18f,steel,.85f,.15f);
    for(int j=0;j<8;++j){float x=-25+55*p-j*3.7f;
     add(0,{x,8.7f,14},{1.45f,.4f,.8f},0,0,steel,.9f,.11f);
     add(0,{x,8.7f,13.17f},{1.25f,.07f,.02f},0,0,pink,0,.2f,pink*3);
    }
    eye={1.4f,1.4f+.5f*p,-25+28*p};target=eye+V3{0,1,22};
   }else if(variant==1){
    // A long maglev overtakes at the left, with an open road ahead of the camera.
    beam({-5.8f,.25f,-90},{-5.8f,.25f,90},.1f,steel,.85f,.13f);
    float nose=-43+110*p;
    for(int j=0;j<16;++j){float z=nose-j*4.8f;
     add(0,{-5.8f,1.7f,z},{1.05f,1.3f,2.2f},0,0,steel,.96f,.09f);
     add(0,{-4.73f,2,z},{.03f,.18f,1.8f},0,0,cyan,0,.2f,cyan*(2+bass));
    }
    for(int j=0;j<12;++j){float z=70-j*12-local*7;
     add(0,{j%2?2.1f:.1f,.2f,z},{.65f,.25f,1.2f},0,0,gold,.9f,.12f);
     add(0,{j%2?2.1f:.1f,.22f,z-1.23f},{.5f,.04f,.025f},0,0,pink,0,.2f,pink*4);
    }
    eye={1.4f,2.1f+1.5f*action,-32+69*p};target=eye+V3{-.3f,.3f,20};
   }else if(variant==2){
    // The street is a suspended diamond circuit; its two banks peel apart.
    for(int j=0;j<18;++j){float z=(j-8.5f)*5.5f;
     for(int side:{-1,1}){float y=2+action*std::max(0.f,5-std::abs(j-8.f))*.7f;
      add(0,{side*(4.5f+action*3),y,z},{3.9f,.24f,2.4f},side*.12f,side*action*.2f,steel,.92f,.1f);
      beam({side*1.2f,y+.3f,z-2.3f},{side*8.f,y+.3f,z+2.3f},.05f,pink,0,.2f,pink*(1.4f+bass));
     }
    }
    eye=mix({0,34,-28},{0,30,-2},p);target={0,3+4*action,14};
   }else if(variant==3){
    // Aerial scale replaces the previous street shots; traffic streams cross below.
    for(int level=0;level<3;++level)for(int j=0;j<24;++j){float z=(j-11.5f)*5-local*(7+level*2),x=-8+level*8.f,y=9+level*9.f;
     add(0,{x,y,z},{.35f,.15f,1.6f},0,0,steel,.9f,.12f);
     add(0,{x,y,z-1.65f},{.28f,.06f,.04f},0,0,level%2?pink:cyan,0,.2f,(level%2?pink:cyan)*4);
    }
    for(int level=0;level<3;++level)beam({-17,10+level*9.f,10+level*14.f},{17,10+level*9.f,10+level*14.f},.12f,steel,.85f,.18f);
    eye=mix({0,48,-44},{0,54,-9},p);target={0,18,15+20*p};
   }else{
    // An entire fleet lifts once from the wet roofs; the camera reveals the horizon.
    for(int j=0;j<70;++j){float x=(j%10-4.5f)*4,z=(j/10-3.f)*8+5,launch=ease(action-(j%7)*.04f),y=4+launch*(20+(j%5)*4);
     add(2,{x,y,z},{.75f,.25f,1.3f},0,launch*.2f,steel,.88f,.14f);
     if(launch>.01f)beam({x,y-.4f,z},{x,y-.4f-launch*4,z},.055f,cyan,0,.2f,cyan*(3+bass));
    }
    add(2,{0,17,70},{17,22,12},.2f,0,gold,.8f,.14f);
    eye=mix({0,3,-24},{0,20,-35},p);target={0,9+action*15,18};
   }
   // Rain only belongs to the ground-level opening and pursuit, not every city view.
   if(variant<2)for(int j=0;j<90;++j){float y=std::fmod(j*2.3f-local*15,23.f);if(y<0)y+=23;
    add(0,{std::sin(j*12.7f)*8,y,(j-45)*1.6f},{.008f,.22f,.008f},0,.12f,cyan,0,.2f,cyan*.45f);
   }
  }else if(world==2){
   if(variant!=3)floor(70,.15f);
   if(variant==0){
    // Parts converge along a conveyor and six gantries assemble a winged engine.
    for(int j=0;j<26;++j){float z=(j-12.5f)*1.3f;
     add(0,{0,-.45f,z},{3,.18f,.42f},0,0,steel,.85f,.2f);
     for(int side:{-1,1})add(0,{side*3.3f,.1f,z},{.09f,.05f,.55f},0,0,pink,0,.2f,pink*(1.4f+bass));
    }
    for(int j=0;j<6;++j){float z=(j-2.5f)*3,close=ease(action+j*.04f);
     for(int side:{-1,1}){beam({side*8.f,-1,z},{side*8.f,7,z},.22f,steel,.9f,.18f);beam({side*8.f,7,z},{side*(6-4*close),3.8f,z},.32f,gold,.88f,.15f);}
    }
    for(int side:{-1,1})for(int j=0;j<10;++j){float attach=ease(action+j*.025f),x=side*(9-7*attach);
     add(0,{x,2+(j%3)*.3f,(j-4.5f)*.7f},{2,.17f,.32f},side*attach*.4f,side*(1-attach)*.4f,steel,.95f,.1f);
    }
    add(2,{0,2,0},{1.2f,1.1f,5},0,0,gold,.9f,.12f);
    eye=mix({18,9,-22},{13,6,-14},p);target={0,2,0};
   }else if(variant==1){
    // A suspended square matrix buckles into a travelling saddle wave.
    for(int x=-7;x<=7;++x)for(int z=-7;z<=7;++z){float wave=std::sin(x*.4f+z*.28f-float(f.localSeconds)*1.8f),height=4+wave*(.3f+action*3);
     add(0,{x*1.7f,height,z*1.7f},{.72f,.12f,.72f},.08f*x,action*wave*.25f,(x+z)%3?steel:gold,.94f,.12f);
     if((x+z)%3==0)add(0,{x*1.7f,height+.15f,z*1.7f},{.55f,.025f,.55f},0,0,cyan,0,.2f,cyan*(.8f+f.spectrum[(x+z+24)%24]*2));
    }
    for(int side:{-1,1})for(int z:{-1,1})beam({side*14.f,-1,z*14.f},{side*14.f,12,z*14.f},.25f,steel,.9f,.2f);
    eye=mix({24,20,-29},{18,14,-25},p);target={0,4,0};
   }else if(variant==2){
    // Ten industrial pistons part a block to reveal a vertical energy heart.
    for(int side:{-1,1})for(int j=0;j<5;++j){float z=(j-2.f)*2.4f,stroke=(.5f+.5f*std::sin(float(f.localSeconds)*2.7f+j*.7f))*(.5f+f.spectrum[j*4]*2);
     beam({side*10.f,3,z},{side*(4+stroke),3,z},.55f,steel,.97f,.1f);
     add(0,{side*(3.2f+action*3),3,z},{.6f,2.6f,.8f},0,0,gold,.9f,.13f);
    }
    for(int side:{-1,1})for(int z:{-1,1})add(0,{side*(1.9f+action*3),3,z*1.9f},{1.65f,3,1.65f},side*action*.15f,0,steel,.96f,.09f);
    beam({0,-.7f,0},{0,8+action*7,0},.14f+impact*.18f,cyan,0,.25f,cyan*(2+action*3+bass));
    eye=mix({20,10,-27},{18,13,-24},p);target={0,4.5f,0};
   }else if(variant==3){
    // Two cargo formations collide once, then their fragments keep separating.
    const float accent=std::max(.01f,float(f.accentSeconds));
    const float duration=float(f.chapterDuration);
    float collide=ease(float(f.localSeconds)/accent);
    float burst=ease((float(f.localSeconds)-accent)/std::max(.25f,duration-accent));
    for(int side:{-1,1})for(int j=0;j<108;++j){float gx=j%6-2.5f,gy=(j/6)%3-1.f,gz=j/18-2.5f;
     // Half width = 2.5*.65 + .28. The leading cube faces meet at x=0
     // exactly at the measured accent, before any scatter is applied.
     V3 packed{side*(17-(17-1.905f)*collide)+gx*.65f,3+gy*.75f,gz*.85f};
     V3 scatter{std::sin(j*2.13f+side)*25,7+std::cos(j*1.37f)*16,std::sin(j*3.71f)*22};
     V3 location=packed+scatter*burst;
     add(j%4?0:2,location,{.28f,.3f,.35f},burst*j*.31f,burst*j*.18f,side<0?steel:gold,.9f,.13f);
     if(j%9==0)beam(location,location-scatter*(burst*.15f),.025f,pink,0,.2f,pink*(2+impact*3));
    }
    if(impact>.01f||burst>.001f)for(int j=0;j<18;++j){float a=j*2*pi/18;
     beam({std::cos(a)*(1+burst*15),3+std::sin(a)*(1+burst*15),1},{std::cos(a)*(2+burst*24),3+std::sin(a)*(2+burst*24),1},.035f,cyan,0,.2f,cyan*(impact*6+burst*.4f));
    }
    eye=mix({8,8,-37},{-5,13,-43},p);target={0,3,0};
   }else{
    // The completed engine launches; a withdrawing gantry gives a readable silhouette.
    float launch=action*ease((p-.25f)/.75f),height=3+launch*36;
    add(2,{0,height,0},{2,1.8f,7},0,pi*.5f,gold,.91f,.1f,{},0,pi*.5f);
    for(int side:{-1,1}){
     add(0,{side*3.f,height-1,0},{3.8f,.18f,2},side*.5f,side*.18f,steel,.97f,.08f);
     beam({side*(5+action*6),-1,-3},{side*(5+action*6),16,-3},.35f,steel,.88f,.18f);
     beam({side*(5+action*6),16,-3},{side*(2+action*8),16,0},.22f,gold,.9f,.15f);
     for(int j=0;j<12;++j){float plume=(j+1)*.7f*(.15f+launch),width=.6f*(1-j/14.f);
      add(2,{side*1.2f,height-2-plume,0},{width,.7f,width},0,0,cyan,0,.2f,cyan*(2.5f+bass));
     }
    }
    add(0,{0,-.6f,0},{8,.5f,10},0,0,steel,.8f,.2f);
    eye={18+launch*5,10+height*.65f,-26};target={0,height,0};
   }
  }else if(world==3){
   floor(55,.06f);
   if(variant==0){
    // A monolithic glass cube separates into eight refracting corners.
    for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1}){
     add(0,{x*(1.6f+action*2.6f),4+y*(1.6f+action*.5f),z*(1.6f+action*2.6f)},{1.55f,1.55f,1.55f},action*x*.15f,action*z*.1f,{.65f,.88f,1},.08f,.025f,{},.95f);
    }
    add(2,{0,4,0},{.65f,2.8f,.65f},.3f,0,gold,.1f,.03f,gold*(.4f+impact));
    beam({-12,4,-4},{12,4,4},.045f,cyan,0,.2f,cyan*(2+bass));
    eye=mix({13,7,-19},{9,9,-17},p);target={0,4,0};
   }else if(variant==1){
    // A line of angled glass monoliths turns one incident beam into a rainbow fan.
    for(int j=0;j<9;++j){float z=(j-4.f)*2.4f,yaw=(j-4.f)*.13f+action*.4f;
     add(0,{0,3,z},{1.8f,3.5f,.24f},yaw,0,{.75f,.9f,1},.05f,.025f,{},.96f);
     V3 e=j%3==0?pink:(j%3==1?cyan:gold);
     beam({0,3,z},{12+action*6,3+(j-4)*(.7f+action*.7f),z+(j-4)*2},.055f,e,0,.2f,e*(2.1f+f.spectrum[j*2]*2));
    }
    beam({-18,3,0},{0,3,0},.08f,{1,1,1},0,.2f,{2.3f,2.3f,2.3f});
    eye=mix({-19,12,-25},{-14,10,-20},p);target={3,3,0};
   }else if(variant==2){
    // A quieter floating book of glass opens into a sparse wing of interior light.
    for(int j=0;j<14;++j){float a=(j-6.5f)*.085f*(.1f+action),x=(j-6.5f)*.45f;
     add(0,{x,3.7f+std::abs(j-6.5f)*action*.28f,0},{.16f,3.2f,3.5f},a,-a,{.65f,.9f,.83f},.05f,.025f,{},.96f);
     beam({x,1,0},{x,5+action*2,0},.035f,gold,0,.2f,gold*(.7f+bass*.5f));
    }
    add(0,{0,-.8f,0},{7,.2f,6},0,0,{.05f,.12f,.17f},.8f,.1f);
    eye=mix({-14,8,-19},{-11,6,-16},p);target={0,4,0};
   }else if(variant==3){
    // A huge faceted prism breaks once into a shell of tumbling glass shards.
    float burst=action*ease((p-.25f)/.75f);
    for(int j=0;j<144;++j){float a=j*2.399963f,y=1-2*(j+.5f)/144.f,r=std::sqrt(std::max(0.f,1-y*y));
     V3 direction{std::cos(a)*r,y,std::sin(a)*r};float shell=2.5f+burst*(10+(j%7)*.8f);
     add(2,{direction.x*shell,4+direction.y*shell,direction.z*shell},{.35f,.8f,.3f},a+burst*j*.07f,burst*j*.12f,j%3?V3{.65f,.9f,1}:V3{1,.8f,.6f},.06f,.025f,{},.94f);
    }
    if(action<.98f)add(2,{0,4,0},{2.8f*(1-action)+.05f,4*(1-action)+.05f,2.8f*(1-action)+.05f},.3f,0,{.65f,.85f,1},.05f,.025f,{},.97f);
    for(int j=0;j<9;++j){float a=j*2*pi/9;beam({0,4,0},{std::cos(a)*20,4+std::sin(a)*14,7},.035f,j%2?cyan:gold,0,.2f,(j%2?cyan:gold)*(.5f+impact*4));}
    eye=mix({7,7,-25},{-3,10,-31},p);target={0,4,0};
   }else{
    // An angular glass avenue opens into a bright, unobstructed exit to Act III.
    for(int j=0;j<16;++j)for(int side:{-1,1}){float z=(j-7.5f)*4.8f,open=ease(action-j*.012f),x=side*(5+open*5);
     add(0,{x,4.8f,z},{2,6,.25f},side*(.2f+open*.5f),side*.22f,{.7f,.9f,1},.06f,.025f,{},.95f);
     beam({side*4.f,-.9f,z},{side*(4+open*4),10,z},.055f,j%2?cyan:gold,0,.2f,(j%2?cyan:gold)*(1.7f+bass));
    }
    // A faceted horizon with visible gaps, rather than a blank luminous wall.
    for(int j=0;j<18;++j){float a=j*pi/17;
     beam({0,5,76},{std::cos(a)*32,5+std::sin(a)*28,82},.08f,j%2?gold:cyan,0,.2f,(j%2?gold:cyan)*(2+bass));
    }
    add(2,{0,5,76},{2.2f,5,2.2f},.4f,0,gold,.6f,.1f,gold*.8f);
    eye={0,2.2f+2.3f*p,-28+66*p};target=eye+V3{0,.8f,20};
   }
  }
  // Architectural surroundings provide depth and scale without putting a wall
  // between the directed camera and the moving hero. Every set uses its own
  // silhouette; the open collision shot deliberately retains negative space.
  V3 dark{.085f,.13f,.19f},stone{.22f,.27f,.32f};
  if(world==0){
   if(variant==0){
    // Folded shipyard horizon and low inset control islands.
    for(int j=0;j<9;++j){float x=(j-4.f)*5.5f,height=8+(j%3)*3.f;
     add(0,{x,height-1,28+(j%2)*4.f},{1.25f,height,2},.1f*(j-4),-.12f*(j-4),dark,.8f,.23f);
     beam({x-1,-.8f,26},{x-1,height*2-1,26},.045f,cyan,0,.25f,cyan*.7f);
    }
    for(int side:{-1,1})for(int j=0;j<8;++j){float z=(j-3.5f)*3.f;
     add(0,{side*6.5f,-.65f,z},{.8f,.45f,.9f},0,0,dark,.8f,.2f);
     add(0,{side*6.5f,-.18f,z},{.55f,.025f,.55f},0,0,cyan,0,.25f,cyan*(.4f+f.spectrum[j*3]*.5f));
    }
   }else if(variant==2){
    // A long elevated aqueduct contrasts with the flowing ribbons below.
    for(int j=0;j<3;++j){float z=24+j*8.f;
     add(0,{0,13,z},{25,.7f,2},0,0,dark,.85f,.24f);
     beam({-25,12,z-2.1f},{25,12,z-2.1f},.08f,gold,0,.25f,gold*.8f);
     for(int side:{-1,1}){add(0,{side*21.f,5,z},{1.3f,6,2},0,side*-.16f,steel,.9f,.18f);beam({side*16.f,-1,z},{side*23.f,12,z},.2f,steel,.9f,.2f);}
    }
    for(int side:{-1,1})for(int j=0;j<6;++j)add(0,{side*8.f,-.65f,(j-2.5f)*4},{1.1f,.45f,1.5f},0,0,dark,.85f,.2f);
   }else if(variant==3){
    // Distant terraced arcologies make the rising gold skyline feel inhabited.
    for(int side:{-1,1})for(int level=0;level<4;++level){float l=float(level);
     add(0,{side*(39-l*2),6+l*9,42+l*3},{14-l*2,5,10-l},side*.18f,0,dark,.7f,.24f);
     beam({side*(39-l*2)-14+l*2,11+l*9,31+l*4},{side*(39-l*2)+14-l*2,11+l*9,31+l*4},.07f,gold,0,.25f,gold*.6f);
    }
    for(int level=0;level<3;++level)add(0,{0,-.9f+level*.15f,0},{14-level*.5f,.15f,11-level*.5f},0,0,dark,.82f,.18f);
   }else if(variant==4){
    // Giant archive slabs frame the mirror book, with illuminated shelf slots.
    for(int side:{-1,1})for(int j=0;j<5;++j){float z=24+j*3.f,x=side*(15+j*.7f),height=11+j*.7f;
     add(0,{x,height-1,z},{1.1f,height,2.2f},side*.12f,side*.18f,dark,.8f,.22f);
     for(int level=0;level<5;++level)add(0,{x-side*1.15f,level*4.f,z-2.3f},{.08f,.08f,2.2f},0,0,gold,0,.25f,gold*.7f);
    }
    add(0,{0,-.65f,0},{12,.45f,19},0,0,dark,.9f,.14f);
   }else if(variant==5){
    for(int side:{-1,1}){
     add(2,{side*22.f,16,49},{7,20,9},side*.3f,side*.15f,dark,.85f,.18f);
     for(int j=0;j<10;++j){float z=-24+j*6.f;add(0,{side*12.f,-.4f,z},{1.6f,.7f,1.8f},0,0,dark,.9f,.16f);add(0,{side*12.f,.33f,z},{1.3f,.04f,.9f},0,0,cyan,0,.25f,cyan*.8f);}
    }
   }
  }else if(world==2){
   if(variant==0){
    // Sawtooth hangar ribs behind the assembly line and service machinery below.
    for(int j=0;j<4;++j){float z=22+j*7.f;
     for(int side:{-1,1}){beam({side*19.f,-1,z},{side*19.f,12,z},.5f,dark,.8f,.24f);beam({side*19.f,12,z},{0,20,z},.4f,steel,.85f,.22f);}
     beam({-19,11,z},{19,11,z},.15f,gold,0,.25f,gold*.65f);
    }
    add(0,{0,6,46},{23,7,1},0,0,dark,.65f,.3f);
    for(int side:{-1,1})for(int j=0;j<6;++j){float z=j*4.f;
     add(0,{side*12.f,-.1f,z},{1.5f,.9f,1.4f},0,0,dark,.8f,.22f);
     for(int vent=0;vent<3;++vent)add(0,{side*12.f,.85f,z+(vent-1)*.5f},{1.2f,.04f,.1f},0,0,steel,.8f,.2f);
    }
   }else if(variant==1){
    // A terraced basin and clustered cooling stacks support the kinetic matrix.
    for(int side:{-1,1})for(int level=0;level<3;++level)add(0,{side*(18+level*2.f),-.5f+level*.55f,0},{.8f,.6f,15},0,0,dark,.75f,.26f);
    for(int side:{-1,1})for(int j=0;j<4;++j){float x=side*(17+j*3.f),height=9+j*2.f;
     add(0,{x,height-1,27+j*2.f},{1.1f,height,2},0,0,dark,.75f,.24f);
     add(0,{x,height*2-.8f,27+j*2.f},{1.3f,.12f,2.2f},0,0,gold,.8f,.2f);
     beam({x,-.8f,24+j*2.f},{x,height*2-1,24+j*2.f},.055f,pink,0,.25f,pink*.7f);
    }
   }else if(variant==2){
    // Laminated containment spines leave a large central opening for the heart.
    for(int side:{-1,1})for(int j=0;j<5;++j){float x=side*(13+j*1.3f),height=8+j*1.5f;
     add(0,{x,height-1,18+j*1.8f},{.45f,height,3.5f},side*.14f,side*.08f,dark,.85f,.2f);
     beam({x,height*2-1,14+j*1.8f},{side*8.f,height*2-1,14+j*1.8f},.1f,gold,0,.25f,gold*.8f);
    }
    for(int side:{-1,1})add(0,{side*12.f,-.4f,0},{1.2f,.7f,7},0,0,dark,.82f,.2f);
    add(0,{0,-.65f,0},{7,.45f,8},0,0,dark,.85f,.18f);
   }else if(variant==3){
    // Two distant derelict hulls supply orbital scale without enclosing the shot.
    for(int side:{-1,1}){V3 centre{side*27.f,-12,58};
     add(0,centre,{17,1.8f,5},side*.16f,side*.12f,dark,.85f,.24f);
     for(int j=0;j<5;++j)add(0,centre+V3{(j-2)*5.f,2.1f,0},{1.4f,.6f,4.6f},side*.16f,0,steel,.85f,.23f);
     beam(centre+V3{-14,0,-5.2f},centre+V3{14,0,-5.2f},.06f,cyan,0,.25f,cyan*.5f);
    }
   }else{
    // Tall spaceport cranes remain behind the launch, with an unobstructed sky.
    for(int side:{-1,1})for(int j=0;j<2;++j){float x=side*(21+j*14.f),z=30+j*18.f,height=35-j*8.f;
     add(0,{x,height*.5f-1,z},{1.6f,height*.5f,3},0,0,dark,.8f,.24f);
     beam({x-side*8.f,height,z},{x+side*11.f,height,z},.6f,steel,.85f,.22f);
     beam({x,height-1,z-3.1f},{x,height-12,z-3.1f},.08f,pink,0,.25f,pink*.8f);
    }
    for(int side:{-1,1})for(int j=0;j<4;++j)add(0,{side*15.f,-.6f,j*6.f},{3,.5f,2.3f},0,0,dark,.8f,.24f);
   }
  }else if(world==3){
   if(variant==0){
    // Refracting triangular sails and a dark stepped base define the observatory.
    for(int side:{-1,1}){
     add(2,{side*18.f,11,24},{7,14,3},side*.3f,side*.12f,{.6f,.8f,.9f},.08f,.03f,{},.95f);
     beam({side*18.f,-1,19},{side*18.f,25,24},.12f,steel,.85f,.18f);
    }
    for(int level=0;level<3;++level)add(0,{0,-.95f+level*.16f,0},{9-level*.45f,.16f,9-level*.45f},0,0,dark,.9f,.1f);
   }else if(variant==1){
    // Floating tuning-fork baffles behind the rainbow, with no enclosing room.
    for(int j=0;j<3;++j){float z=26+j*7.f,y=11+j*4.f;
     for(int side:{-1,1}){add(0,{side*19.f,y-4,z},{1.3f,7,2},0,side*.12f,dark,.85f,.22f);beam({side*19.f,y+3,z},{side*3.f,y+3,z},.3f,steel,.9f,.18f);}
    }
    for(int j=0;j<9;++j)add(0,{0,-.7f,(j-4)*2.4f},{3,.4f,.7f},0,0,dark,.88f,.15f);
   }else if(variant==2){
    // A sparse brutalist retreat keeps this chapter quiet but gives it a place.
    for(int level=0;level<3;++level)add(0,{0,-.7f+level*.2f,7+level*4.f},{15-level*2.f,.2f,12-level*2.f},0,0,stone,.2f,.48f);
    add(0,{-16,9,23},{3,10,3},-.16f,-.18f,stone,.25f,.45f);
    add(0,{14,12,27},{3.5f,13,3},.15f,.12f,stone,.25f,.45f);
    beam({-16,19,23},{14,25,27},.32f,steel,.85f,.22f);
   }else if(variant==3){
    // A broad angular gantry gives the expanding glass cloud a readable scale.
    for(int side:{-1,1}){beam({side*22.f,-1,29},{side*24.f,14,29},.4f,dark,.85f,.22f);beam({side*24.f,14,29},{side*8.f,25,29},.4f,steel,.9f,.18f);}
    beam({-8,25,29},{8,25,29},.18f,gold,0,.25f,gold*.8f);
    for(int side:{-1,1})for(int j=0;j<4;++j)add(0,{side*(16+j*2.f),-.5f,10+j*6.f},{1.4f,.6f,2.5f},side*.2f,0,dark,.82f,.2f);
   }else{
    for(int side:{-1,1}){
     add(0,{side*11.f,-.5f,10},{1.7f,.6f,50},0,0,dark,.85f,.18f);
     add(2,{side*31.f,16,94},{8,20,5},side*.3f,side*.1f,stone,.5f,.28f);
    }
   }
  }
  if(world>=4){
   // Legacy Act III RT path is retained for compatibility, outside Act II direction.
   if(world==7)add(0,{0,-1.4f,0},{80,.3f,80},0,0,{.45f,.5f,.57f},.95f,.09f);
   act3Geometry(f);
   if(world!=4)for(int ring=0;ring<2;++ring)for(int band=0;band<24;++band){float a=band*2*pi/24+t*(ring?-.07f:.09f),r=8+ring*2.f,hbar=.08f+f.spectrum[band]*1.8f;V3 e=band%2?cyan:pink;add(0,{std::sin(a)*r,hbar-1,std::cos(a)*r},{.09f,hbar,.09f},a,0,e,.6f,.1f,e*(.4f+f.spectrum[band]*2));}
   float a=t*.13f+f.shot*pi*.5f,r=world==4?19.f:(world==5?17.f:(world==6?25.f:24.f));r+=std::sin(f.progress*pi)*1.2f;
   float y=world==4?6.f:(world==5?6.5f:8.f);eye={std::sin(a)*r,y+std::sin(t*.17f)*1.5f,std::cos(a)*r};target={0,world==4?2.7f:(world==5?3.5f:3.4f),0};
  }
  V3 forward=norm(target-eye),right=norm(cross(forward,{0,1,0})),up=cross(right,forward);
  Parameters result{};auto put=[](float* p,V3 v){p[0]=v.x;p[1]=v.y;p[2]=v.z;};put(result.eye,eye);put(result.forward,forward);put(result.right,right);put(result.up,up);result.clock[0]=float(w);result.clock[1]=float(h);result.clock[2]=t;result.clock[3]=float(world);result.music[0]=bass;result.music[1]=f.hit;result.music[2]=f.energy;result.music[3]=f.drive;std::copy(f.spectrum.begin(),f.spectrum.end(),result.bands);return result;
 }
 const float* render(const TimelineFrame& f,int w,int h){
  if(!rtViewportFits(uint32_t(w),uint32_t(h),maxOutputBytes))throw std::runtime_error("RT viewport exceeds this device's storage buffer range; reduce window size");
  if(w!=width||h!=height){check(vkDeviceWaitIdle(device));if(output)release(*output);if(readback)release(*readback);output=buffer(VkDeviceSize(w)*h*4*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);readback=buffer(output->size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);descriptor(1,output,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);width=w;height=h;}
  auto params=scene(f,w,h);std::memcpy(parameterUpload->mapped,&params,sizeof(params));std::memcpy(instanceBuffer->mapped,instances.data(),instances.size()*sizeof(instances[0]));std::memcpy(objectUpload->mapped,objectData.data(),objectData.size()*sizeof(Object));
  auto geometry=instanceGeometry();VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.dstAccelerationStructure=tlas.handle;build.geometryCount=1;build.pGeometries=&geometry;build.scratchData.deviceAddress=scratchAddress(scratch);VkAccelerationStructureBuildRangeInfoKHR range{};range.primitiveCount=uint32_t(instances.size());const auto* ranges=&range;
  begin();
  barrier(VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
  VkBufferCopy parameterCopy{0,0,sizeof(params)},objectCopy{0,0,objectData.size()*sizeof(Object)};
  vkCmdCopyBuffer(cmd,parameterUpload->buffer,parameters->buffer,1,&parameterCopy);
  vkCmdCopyBuffer(cmd,objectUpload->buffer,objects->buffer,1,&objectCopy);
  barrier(VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  vkCmdBuildAccelerationStructuresKHR(cmd,1,&build,&ranges);barrier(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&set,0,nullptr);vkCmdDispatch(cmd,(w+7)/8,(h+7)/8,1);barrier(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferCopy imageCopy{0,0,output->size};vkCmdCopyBuffer(cmd,output->buffer,readback->buffer,1,&imageCopy);
  barrier(VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT);
  submit();++frames;return static_cast<const float*>(readback->mapped);
 }
};
HardwareRT::HardwareRT()=default;
HardwareRT::~HardwareRT()=default;
bool HardwareRT::initialize(const std::filesystem::path& shader,bool validation,std::string& reason){try{p_=std::make_unique<Impl>();p_->initialize(shader,validation);reason=p_->name;return true;}catch(const std::exception& e){reason=e.what();p_.reset();return false;}}
const float* HardwareRT::render(const TimelineFrame& f,int w,int h){if(!p_)throw std::runtime_error("RT backend not initialized");return p_->render(f,w,h);}
std::string HardwareRT::diagnostics() const{return p_?"Vulkan KHR ray queries / "+p_->name+" / RT frames="+std::to_string(p_->frames):"RT unavailable";}
}
