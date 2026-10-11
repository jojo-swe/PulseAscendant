// Capability discovery only. This executable does not render ray-traced images.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static void check(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("Vulkan error "+std::to_string(result));}
static std::string quote(const char* s){
 std::string out="\"";for(;*s;++s){if(*s=='\"'||*s=='\\')out+='\\';if(static_cast<unsigned char>(*s)>=32)out+=*s;}return out+'\"';
}
int main(){
 VkInstance instance=VK_NULL_HANDLE;
 try{
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName="Pulse Ascendant Vulkan capability probe";app.apiVersion=VK_API_VERSION_1_2;
  VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&app;
  check(vkCreateInstance(&create,nullptr,&instance));
  uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,nullptr));
  std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
  std::cout<<std::boolalpha<<"{\"probe_only\":true,\"devices\":[";bool first=true;
  for(auto device:devices){
   VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(device,&props);
   uint32_t n=0;check(vkEnumerateDeviceExtensionProperties(device,nullptr,&n,nullptr));
   std::vector<VkExtensionProperties> extensions(n);check(vkEnumerateDeviceExtensionProperties(device,nullptr,&n,extensions.data()));
   auto has=[&](const char* name){return std::any_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,name)==0;});};
   VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
   VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
   VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
   VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
   VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
   void** tail=&features.pNext;
   if(props.apiVersion>=VK_API_VERSION_1_2||has(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)){*tail=&bda;tail=&bda.pNext;}
   if(has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)){*tail=&as;tail=&as.pNext;}
   if(has(VK_KHR_RAY_QUERY_EXTENSION_NAME)){*tail=&query;tail=&query.pNext;}
   if(has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)){*tail=&pipeline;tail=&pipeline.pNext;}
   vkGetPhysicalDeviceFeatures2(device,&features);
   const bool deferred=has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
   const bool base=props.apiVersion>=VK_API_VERSION_1_2&&bda.bufferDeviceAddress&&as.accelerationStructure&&deferred;
   if(!first)std::cout<<',';first=false;
   std::cout<<"{\"name\":"<<quote(props.deviceName)<<",\"api_version\":"<<props.apiVersion
    <<",\"acceleration_structure\":"<<bool(as.accelerationStructure)
    <<",\"buffer_device_address\":"<<bool(bda.bufferDeviceAddress)
    <<",\"deferred_host_operations\":"<<deferred
    <<",\"ray_query\":"<<bool(query.rayQuery)<<",\"ray_tracing_pipeline\":"<<bool(pipeline.rayTracingPipeline)
    <<",\"act2_ray_query_candidate\":"<<bool(base&&query.rayQuery)<<'}';
  }
  std::cout<<"]}\n";vkDestroyInstance(instance,nullptr);return 0;
 }catch(const std::exception& e){if(instance)vkDestroyInstance(instance,nullptr);std::cerr<<e.what()<<'\n';return 1;}
}
