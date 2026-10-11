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
  if(world<4||world==7)add(0,{0,-1.4f,0},{80,.3f,80},0,0,{.45f,.5f,.57f},.95f,.09f);
  if(world==0){
   for(int z=-6;z<=6;++z){add(3,{0,-1,z*9.f},{6,7,4},0,0,steel,.96f,.1f);add(3,{0,-1,z*9.f-.2f},{5.7f,6.7f,.6f},0,0,cyan,0,.2f,cyan*(1.5f+f.hit*4));for(int j=0;j<=12;++j){float a=j*pi/12;add(1,{std::cos(a)*6,-1+std::sin(a)*7,z*9.f},{.18f,.18f,.18f},0,0,gold,.9f,.1f);}}
   for(int i=0;i<24;++i){float a=i*2*pi/24+t*.2f;add(1,{std::sin(a)*3,2+std::sin(a*3+t)*.7f,std::cos(a)*3},{.2f,.2f,.2f},0,0,gold,.9f,.1f);}
   add(1,{0,2,0},{1.1f+bass*.2f,1.1f+bass*.2f,1.1f+bass*.2f},0,0,gold,.95f,.05f);
  }else if(world==1){
   // Layered distant skyline closes the view beyond the fast foreground avenue.
   for(int j=0;j<48;++j){float a=j*2*pi/48,r=105+12*std::sin(j*3.7f),height=9+8*(.5f+.5f*std::sin(j*7.1f));V3 position{std::sin(a)*r,height-1,std::cos(a)*r};add(0,position,{3.5f,height,3.5f},a,0,{.06f,.11f,.18f},.45f,.3f);add(0,position+V3{0,height+.03f,0},{3.5f,.03f,3.5f},a,0,cyan,0,.2f,cyan*.7f);}
   for(int side:{-1,1})for(int z=-10;z<=10;++z)add(0,{side*3.f,-1.085f,z*7.f},{.035f,.008f,2.7f},0,0,cyan,0,.2f,cyan*(1+f.bass*2));
   for(int j=0;j<12;++j){float z=std::fmod(j*13.f-t*(j%2?7.f:-9.f),140.f);if(z<0)z+=140;z-=70;float x=j%2?2.2f:-2.2f;add(0,{x,.1f+std::sin(t+j)*.12f,z},{.65f,.22f,1.2f},0,0,steel,.95f,.09f);add(0,{x,.1f,z-1.21f},{.5f,.035f,.025f},0,0,pink,0,.2f,pink*5);}
   for(int z=-12;z<=12;++z)for(int side:{-1,1})for(int row=0;row<3;++row){float seed=std::sin(float(z*37+row*19+side)*17.3f)*.5f+.5f,height=3+seed*13;float x=side*(8.f+row*6);add(0,{x,height-1,z*6.f},{2.4f,height,2.3f},0,0,{.09f,.13f,.2f},.65f,.2f);for(int j=0;j<5;++j){V3 e=j%2?pink:cyan;add(0,{x-side*2.42f,j*height*.37f,z*6.f},{.025f,.055f,1.8f},0,0,e,0,.2f,e*(1+f.spectrum[(j*4+row)%24]*4));}}
   for(int i=0;i<120;++i){float z=std::fmod(i*7.73f+t*14,140.f)-70,x=std::sin(i*12.7f)*6,y=std::fmod(i*2.3f-t*16,18.f);if(y<0)y+=18;add(0,{x,y,z},{.008f,.3f,.008f},0,.08f,cyan,0,.2f,cyan*.7f);}
  }else if(world==2){
   for(int ring=0;ring<4;++ring)add(3,{0,1.6f+ring*.4f,0},{4.4f+ring*.7f,4.4f+ring*.7f,4},t*(ring%2?.2f:-.16f),0,ring%2?gold:steel,.97f,.08f,{},0,pi*.5f+std::sin(t*.2f+ring)*.16f);
   for(int ring=0;ring<5;++ring)for(int j=0;j<32;++j){float a=j*2*pi/32+t*(ring%2?.15f:-.2f),r=3+ring*.7f;add(0,{std::sin(a)*r,ring*.8f,std::cos(a)*r},{.2f,.16f,.55f},-a,0,ring%2?gold:steel,.9f,.15f);}
   for(int j=0;j<8;++j){float a=j*2*pi/8-t*.3f;add(0,{std::sin(a)*7,2,std::cos(a)*7},{.6f,3.2f,.6f},-a,0,steel,.95f,.13f);add(1,{std::sin(a)*7,5.3f,std::cos(a)*7},{.3f,.3f,.3f},0,0,pink,0,.2f,pink*(3+f.hit*6));}
   add(2,{0,2,0},{1.8f,2.8f,1.8f},t*.3f,t*.16f,gold,.8f,.1f);
  }else if(world==3){
   for(int j=0;j<16;++j){float a=j*2*pi/16,r=j%2?5.f:3.2f;add(2,{std::sin(a)*r,1.8f+std::sin(t*.7f+j)*.5f,std::cos(a)*r},{.75f,1.9f+f.spectrum[j]*.7f,.75f},a+t*.14f,.15f*std::sin(t+j),j%2?cyan:gold,.15f,.035f,{},.93f);}
   add(2,{0,2.2f,0},{1.8f,3.2f,1.8f},t*.23f,.2f*std::sin(t*.5f),{.7f,.9f,1},.1f,.02f,{},.96f);
   for(int j=0;j<24;++j){float a=j*2*pi/24;add(0,{std::sin(a)*9,3,std::cos(a)*9},{.15f,4,.15f},a,0,pink,.6f,.1f);}
  }
  if(world>=4)act3Geometry(f);
  // Actual 3D spectrum geometry participates in RT visibility and reflections.
  if(world!=1&&world!=4)for(int ring=0;ring<2;++ring)for(int band=0;band<24;++band){float a=band*2*pi/24+t*(ring?-.07f:.09f),r=8+ring*2.f,hbar=.08f+f.spectrum[band]*1.8f;V3 e=band%2?cyan:pink;add(0,{std::sin(a)*r,hbar-1,std::cos(a)*r},{.09f,hbar,.09f},a,0,e,.6f,.1f,e*(.4f+f.spectrum[band]*2));}
  V3 eye{},target{0,1.4f,0};float a=t*.13f+f.shot*pi*.5f;
  if(world==0){eye={std::sin(t*.23f)*2,1.2f+std::sin(t*.11f)*.5f+f.shot*.25f,std::fmod(t*2+f.shot*3,70.f)-35};target=eye+V3{std::sin(t*.13f)*.2f,-.15f,5};}
  else if(world==1){eye={std::sin(t*.17f)*2,1.2f+std::sin(t*.2f)*.7f+f.shot*.25f,std::fmod(t*5+f.shot*7,110.f)-55};target=eye+V3{std::sin(t*.1f),-.25f,8};}
  else{float r=(world==2?14.f:12.5f)+std::cos(f.progress*pi)*.8f;eye={std::sin(a)*r,4.5f+std::sin(t*.2f)*1.4f,std::cos(a)*r};}
  if(world>=4){float r=world==4?19.f:(world==5?17.f:(world==6?25.f:24.f));r+=std::sin(f.progress*pi)*1.2f;float y=world==4?6.f:(world==5?6.5f:8.f);eye={std::sin(a)*r,y+std::sin(t*.17f)*1.5f,std::cos(a)*r};target={0,world==4?2.7f:(world==5?3.5f:3.4f),0};}
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
