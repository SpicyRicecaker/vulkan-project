#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>
// #define VMA_IMPLEMENTATION
// #include "vk_mem_alloc.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <iostream>
#include <vector>

// api for glsl -> spirv conversion
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <filesystem>
#include <fstream>
#include <shaderc/shaderc.hpp>
// might need to change this include dir based on OS
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "lib.h"

#ifdef __APPLE__
const char* os = "macos";
#elif _WIN32
const char* os = "windows";
#endif

using namespace std;
using namespace fmt;
using namespace filesystem;

#define u32 uint32_t

const u32 WIDTH = 800;
const u32 HEIGHT = 600;

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const vector<const char*> wanted_device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    // unneeded on vulkan 1.3, see
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/enabling_buffer_device_address.html
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};

struct DepthImage {
  VkImage image;
  VmaAllocation allocation;
};

struct Semaphores {
  VkSemaphore swapchain_image_is_available;
  VkSemaphore rendering_is_complete;
};

struct Fences {
  VkFence command_buffer_can_be_used;
  VkFence rendering_is_complete;
};

void VK_CHECK_CONDITIONAL(VkResult result,
                          string err,
                          vector<VkResult> optionals) {
  bool valid = false;
  for (auto o : optionals) {
    if (o == result) {
      valid = true;
      break;
    }
  }

  if (!valid && result != VK_SUCCESS) {
    println("{}", string_VkResult(result));
    throw runtime_error(err);
  }
}

void VK_CHECK(VkResult result, string err) {
  if (result != VK_SUCCESS) {
    println("{}", string_VkResult(result));
    throw runtime_error(err);
  }
}

class App {
 public:
  GLFWwindow* window;
  VmaAllocator _allocator;
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkPhysicalDevice physical_device;
  u32 physical_device_index;
  VkSurfaceKHR surface;
  VkDevice device;
  VkRenderPass render_pass;
  VkSwapchainKHR swapchain;
  vector<VkImage> swapchain_images;
  vector<VkImageView> swapchain_image_views;
  VkFormat swapchain_image_format;
  VkColorSpaceKHR swapchain_image_colorspace;
  VkFormat depth_buffer_format;
  VkImageView depth_buffer_view;
  DepthImage depth_image;
  vector<VkPipeline> pipelines;
  VkExtent2D swapchain_image_extent;
  vector<VkFramebuffer> swapchain_framebuffers;
  Pipeline pipeline_constructor;
  DeletionStack deletion_stack;
  vector<VkCommandBuffer> command_buffers;
  VkQueue queue;
  VkCommandPool command_pool;
  Semaphores semaphores;
  Fences fences;
  u32 frame_count = 0;

  int window_width;
  int window_height;

  void run() {
    init_window();
    init_vulkan();
    main_loop();
    cleanup();
  }

 private:
  void init_window() {
    glfwInit();

    // disable opengl context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Window", nullptr, nullptr);

    glfwGetWindowSize(window, &window_width, &window_height);
  }

  // All messenger functions must have the signature
  static VkBool32 debug_messenger_callback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
      void* pUserData) {
    // if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    // {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      cerr << pCallbackData->pMessage << endl;
    }
    return VK_FALSE;
  }

  bool layers_exists(vector<const char*>* requested_layers) {
    u32 layers_count = 0;
    vkEnumerateInstanceLayerProperties(&layers_count, nullptr);
    vector<VkLayerProperties> available_layers(layers_count);
    // for some reason, you need to include the &layers_count ref here
    // again, otherwise you get a sigsev
    vkEnumerateInstanceLayerProperties(&layers_count, available_layers.data());

    // debug
    // for (auto& available_layer : available_layers) {
    //   cout << available_layer.layerName << endl;
    // }

    for (auto& requested_layer : *requested_layers) {
      bool requested_layer_exists = false;
      for (auto& available_layer : available_layers) {
        if (!strcmp(requested_layer, available_layer.layerName)) {
          requested_layer_exists = true;
          break;
        }
      }

      if (!requested_layer_exists) {
        return false;
      }
    }

    return true;
  }

  void dbg_get_available_instance_extensions() {
    u32 extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    cout << extension_count << " extensions supported" << endl;

    vector<VkExtensionProperties> v_extensions(extension_count);

    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                           v_extensions.data());

    for (auto& a : v_extensions) {
      cout << a.extensionName << endl;
    }
  }

  vector<const char*> get_required_instance_extensions() {
    // debug
    assert(glfwVulkanSupported());

    // query glfw for the extensions it needs
    u32 extensions_count;
    // guarantees the inclusion of VK_KHR_surface
    // if on MacOS, includes VK_EXT_metal_surface
    const char** t_extensions =
        glfwGetRequiredInstanceExtensions(&extensions_count);

    // debug
    // cout << "there are " << extensions_count << " required glfw
    // extensions."
    //      << endl;

    vector<const char*> extensions;
    for (unsigned int i = 0; i < extensions_count; i++) {
      extensions.emplace_back(t_extensions[i]);
    }
    // add the khr 2 extension
    extensions.emplace_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    if (strcmp(os, "macos") == 0) {
      extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    // debug
    // for (auto& e : extensions) {
    //   cout << e << endl;
    // }

    if (enableValidationLayers) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
  }

  void create_instance() {
    VkApplicationInfo app_info{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pApplicationName = "Vulkan",
                               .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
                               .pEngineName = "No Engine",
                               .engineVersion = VK_MAKE_VERSION(0, 1, 0),
                               .apiVersion = VK_API_VERSION_1_3};

    // get required extensions
    auto extensions = get_required_instance_extensions();

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (strcmp(os, "macos") == 0) {
      create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};
    if (enableValidationLayers && !layers_exists(&validation_layers)) {
      throw runtime_error(
          "validation layers requested but validation layer "
          "does not exist on device");
    }

    // this is the beauty of cpp right here
    // ensure the VK_LAYER_KHRONOS_validation is available on the current
    // gpu
    if (enableValidationLayers) {
      create_info.enabledLayerCount =
          static_cast<u32>(validation_layers.size());
      create_info.ppEnabledLayerNames = validation_layers.data();
      VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info =
          get_debug_messenger_info();
      create_info.pNext = &debug_messenger_info;
    }

    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
      throw runtime_error("failed to create instance");
    };
    deletion_stack.push(
        [this]() { vkDestroyInstance(this->instance, nullptr); });
  }

  VkResult CreateDebugUtilsMessengerEXT(
      VkInstance instance,
      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
      const VkAllocationCallbacks* pAllocator,
      VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
      return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
      return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
  }

  void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                     VkDebugUtilsMessengerEXT messenger,
                                     const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
      func(instance, messenger, pAllocator);
    }
  }

  // create debug messenger
  VkDebugUtilsMessengerCreateInfoEXT get_debug_messenger_info() {
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_messenger_callback,
    };
    return debug_messenger_info;
  }

  void setup_debug_messenger() {
    if (!enableValidationLayers) {
      return;
    }
    // can't directly call `vkCreateDebugUtilsMessengerEXT`, since for some
    // reason it's an extension function that is not loaded by default
    // could be a usecase for `volk`
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info =
        get_debug_messenger_info();
    if (CreateDebugUtilsMessengerEXT(instance, &debug_messenger_info, nullptr,
                                     &debug_messenger) != VK_SUCCESS) {
      throw runtime_error("failed to create debug messenger");
    };
    deletion_stack.push([this]() { this->destroy_debug_messenger(); });
  }

  void choose_physical_device() {
    u32 physical_device_count;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);
    vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count,
                               physical_devices.data());

    int best_score = -1;
    u32 best_physical_device_index;
    u32 i = 0;
    VkPhysicalDevice best_physical_device = VK_NULL_HANDLE;

    for (auto l_physical_device : physical_devices) {
      VkPhysicalDeviceProperties2 device_properties{
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      };
      vkGetPhysicalDeviceProperties2(l_physical_device, &device_properties);

      int current_score = 0;

      if (device_properties.properties.deviceType ==
          VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        current_score += 1000;
      }

      if (current_score >= best_score) {
        best_physical_device = l_physical_device;
        best_score = current_score;
        best_physical_device_index = i;
      }
      i++;
    }
    // DEBUG
    // VkPhysicalDeviceProperties device_properties;
    // vkGetPhysicalDeviceProperties(best_physical_device, &device_properties);
    // cout << device_properties.deviceName << endl;
    //
    this->physical_device = best_physical_device;
    this->physical_device_index = best_physical_device_index;
  }

  void dbg_get_surface_output_formats() {
    // debug surface properties - get output rgba formats
    VkPhysicalDeviceSurfaceInfo2KHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .surface = surface};

    // get supported formats
    u32 surface_formats_count;
    vkGetPhysicalDeviceSurfaceFormats2KHR(physical_device, &surface_info,
                                          &surface_formats_count, nullptr);

    vector<VkSurfaceFormat2KHR> surface_formats(surface_formats_count);
    vkGetPhysicalDeviceSurfaceFormats2KHR(physical_device, &surface_info,
                                          &surface_formats_count,
                                          surface_formats.data());

    for (auto& surface_format : surface_formats) {
      cout << surface_format.surfaceFormat.colorSpace << endl;
    }
  }

  struct QueueFamilyIndex {
    std::optional<u32> draw_and_present_family;

    bool isComplete() { return draw_and_present_family.has_value(); };
  };

  QueueFamilyIndex find_queue_family_index() {
    // get queue device capabilities from physical device??
    u32 queue_family_property_count;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &queue_family_property_count, nullptr);
    vector<VkQueueFamilyProperties> queue_family_properties(
        queue_family_property_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                             &queue_family_property_count,
                                             queue_family_properties.data());

    std::optional<u32> draw_and_present_family;

    int i = 0;
    for (auto& p : queue_family_properties) {
      if ((p.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface,
                                             &present_support);
        if (present_support) {
          draw_and_present_family = i;
          break;
        }
      }
      i++;
    }
    return QueueFamilyIndex{.draw_and_present_family = draw_and_present_family};
  }

  bool device_includes_extensions() {
    u32 device_property_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr,
                                         &device_property_count, nullptr);
    std::vector<VkExtensionProperties> device_extensions(device_property_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr,
                                         &device_property_count,
                                         device_extensions.data());
    // DBG
    // for (auto& p : device_extensions) {
    //   println("{}", p.extensionName);
    // }

    bool includes = false;
    for (auto& wde : wanted_device_extensions) {
      for (auto& de : device_extensions) {
        if (strcmp(de.extensionName, wde) == 0) {
          includes = true;
          break;
        }
      }
    }
    return includes;
  }

  bool is_device_suitable() {
    // check if device contains VK_KHR_buffer_device_address support
    bool buffer_device_address_support = false;
    {
      VkPhysicalDeviceBufferDeviceAddressFeatures features{
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
      };

      VkPhysicalDeviceFeatures2 device_features = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
          .pNext = &features};
      vkGetPhysicalDeviceFeatures2(physical_device, &device_features);

      if (features.bufferDeviceAddress == VK_TRUE) {
        buffer_device_address_support = true;
      }
    }

    QueueFamilyIndex queue_family_index = find_queue_family_index();

    bool swapchain_support = false;

    // Check if device supports swapchain extension
    return queue_family_index.isComplete() && device_includes_extensions();
  }

  void create_logical_device() {
    if (!is_device_suitable()) {
      throw runtime_error("device not suitable for various reasons");
    }

    QueueFamilyIndex queue_family_index = find_queue_family_index();

    // only need 1 device for gaming lol
    vector<float> queue_priorities = {1.0};

    VkDeviceQueueCreateInfo device_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family_index.draw_and_present_family.value(),
        .queueCount = 1,
        .pQueuePriorities = queue_priorities.data()};

    vector<VkDeviceQueueCreateInfo> device_queue_create_infos = {
        device_queue_create_info};

    // not DRY code but whatever trevor
    // need to add VK_KHR_portability_subset for whatever reason
    // not sure if validation happens for device errors without explicitly
    // setting it again (after initially setting it in instance)
    vector<const char*> extensions;
    if (strcmp(os, "macos") == 0) {
      extensions.emplace_back("VK_KHR_portability_subset");
    }

    // also put wanted device extensions here
    for (auto e : wanted_device_extensions) {
      extensions.emplace_back(e);
    }

    vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};
    if (enableValidationLayers && !layers_exists(&validation_layers)) {
      throw runtime_error(
          "validation layers requested but validation layer "
          "does not exist on device");
    }

    VkPhysicalDeviceBufferDeviceAddressFeatures
        physical_device_buffer_device_address_features = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
            .bufferDeviceAddress = VK_TRUE};

    VkPhysicalDeviceFeatures2 physical_device_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &physical_device_buffer_device_address_features};

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &physical_device_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = device_queue_create_infos.data(),
        .enabledLayerCount = static_cast<u32>(validation_layers.size()),
        .ppEnabledLayerNames = validation_layers.data(),
        .enabledExtensionCount = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (vkCreateDevice(physical_device, &device_create_info, nullptr,
                       &device) != VK_SUCCESS) {
      throw runtime_error("unable to create device");
    };
    deletion_stack.push([this]() { vkDestroyDevice(this->device, nullptr); });

    // queues can be requested later based on the logical device
    VkQueue queue;
    vkGetDeviceQueue(device, queue_family_index.draw_and_present_family.value(),
                     0, &queue);
  }

  void create_surface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
        VK_SUCCESS) {
      throw runtime_error("unable to create surface");
    };
    deletion_stack.push([this]() {
      vkDestroySurfaceKHR(this->instance, this->surface, nullptr);
    });
  }

  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    vector<VkSurfaceFormatKHR> formats;
    vector<VkPresentModeKHR> presentModes;
  };

  SwapChainSupportDetails get_swapchain_support() {
    SwapChainSupportDetails swapchain_support_details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device, surface, &swapchain_support_details.capabilities);

    u32 surface_formats_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                         &surface_formats_count, nullptr);
    swapchain_support_details.formats.resize(surface_formats_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &surface_formats_count,
        swapchain_support_details.formats.data());

    u32 present_modes_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                              &present_modes_count, nullptr);
    swapchain_support_details.presentModes.resize(present_modes_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_modes_count,
        swapchain_support_details.presentModes.data());

    return swapchain_support_details;
  }

  void create_swapchain() {
    // query formats supported by surface
    SwapChainSupportDetails swapchain_support_details = get_swapchain_support();

    bool format_valid = false;
    for (auto& f : swapchain_support_details.formats) {
      // cout << "[" << endl;
      // cout << string_VkFormat(f.format) << endl;
      // cout << string_VkColorSpaceKHR(f.colorSpace) << endl;
      // cout << "]" << endl;
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        format_valid = true;
        break;
      }
    }

    u32 min_image_count =
        swapchain_support_details.capabilities.maxImageCount == 0
            ? swapchain_support_details.capabilities.minImageCount + 1
            : min(swapchain_support_details.capabilities.minImageCount + 1,
                  swapchain_support_details.capabilities.maxImageCount);

    if (!format_valid) {
      // cout << get_swapchain_support() << endl;
      throw runtime_error("swapchain doesn't have supported formats");
    }

    swapchain_image_extent = {.width = static_cast<u32>(window_width),
                              .height = static_cast<u32>(window_height)};

    QueueFamilyIndex index = find_queue_family_index();

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = min_image_count,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapchain_image_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        // only good if we have one queue writing to the swapchain??
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        // .queueFamilyIndexCount = 1,
        // .pQueueFamilyIndices = nullptr,
        .preTransform = swapchain_support_details.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = VK_TRUE};

    if (vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr,
                             &swapchain) != VK_SUCCESS) {
      throw runtime_error("failed to create swapchain");
    }
    deletion_stack.push([this]() {
      vkDestroySwapchainKHR(this->device, this->swapchain, nullptr);
    });

    u32 image_count;
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
    swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count,
                            swapchain_images.data());
    swapchain_image_format = swapchain_create_info.imageFormat;
    swapchain_image_colorspace = swapchain_create_info.imageColorSpace;
  }

  void create_depth_buffer() {
    depth_buffer_format = VK_FORMAT_D32_SFLOAT;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_buffer_format,
        .extent = {.width = static_cast<u32>(window_width),
                   .height = static_cast<u32>(window_height),
                   .depth = static_cast<u32>(1)},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    // see https://vkguide.dev/docs/chapter-3/depth_buffer/
    VmaAllocationCreateInfo image_alloc_info = {};
    image_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    image_alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (vmaCreateImage(_allocator, &image_info, &image_alloc_info,
                       &depth_image.image, &depth_image.allocation,
                       nullptr) != VK_SUCCESS) {
      cerr << "error creating depth image" << endl;
    };

    deletion_stack.push([this]() {
      vmaDestroyImage(this->_allocator, this->depth_image.image,
                      this->depth_image.allocation);
    });
  }

  void create_render_pass() {
    vector<VkAttachmentDescription> attachments = {
        // color
        {
            .format = swapchain_image_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        // depth
        {.format = depth_buffer_format,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         // can't explicitly only use depth without the stencil without a flag
         .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};

    VkAttachmentReference attachment_references[] = {
        // color
        {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        // depth
        {.attachment = 1,
         .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
    };

    VkSubpassDescription subpasses[] = {{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        // .inputAttachmentCount = 0,
        // .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment_references[0],
        .pDepthStencilAttachment = &attachment_references[1],
    }};

    VkDependencyInfo dependencies[] = {
        {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
         .imageMemoryBarrierCount = 2,
         .pImageMemoryBarriers = nullptr},
    };

    VkSubpassDependency subpass_dependencies[] = {{
        // https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/
        // vk_subpass_external transitions the image layout automatically
        // however, it makes no guarantee on when this transition happens
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        // specify that we wait until the color_attachment_output stage until we
        // make the transition.
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    }};

    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<u32>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = subpasses,
        .dependencyCount = 1,
        .pDependencies = subpass_dependencies};

    if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) !=
        VK_SUCCESS) {
      throw runtime_error("failed to create render pass");
    };
    deletion_stack.push([this]() {
      vkDestroyRenderPass(this->device, this->render_pass, nullptr);
    });
  }

  VkImageView create_image_view(VkImage image,
                                VkFormat format,
                                VkImageAspectFlags flags) {
    VkImageView image_view;
    VkImageViewCreateInfo image_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        // keep channels the same
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange = {
            .aspectMask = flags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }};
    if (vkCreateImageView(device, &image_view_info, nullptr, &image_view) !=
        VK_SUCCESS) {
      throw runtime_error("failed to create image view!");
    }
    deletion_stack.push(
        [=, this]() { vkDestroyImageView(this->device, image_view, nullptr); });

    return image_view;
  }

  void create_depth_buffer_view() {
    depth_buffer_view = create_image_view(
        depth_image.image, depth_buffer_format, VK_IMAGE_ASPECT_DEPTH_BIT);
  }

  // image views used at runtime during pipeline rendering
  void create_image_views() {
    swapchain_image_views.resize(swapchain_images.size());
    auto i = 0;
    for (auto swapchain_image : swapchain_images) {
      swapchain_image_views[i] = create_image_view(
          swapchain_image, swapchain_image_format, VK_IMAGE_ASPECT_COLOR_BIT);
      i++;
    }
  }

  void create_command_pool() {
    QueueFamilyIndex queue_family_index = find_queue_family_index();

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index.draw_and_present_family.value()};
    ;

    if (vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                            &command_pool) != VK_SUCCESS) {
      throw runtime_error("failed to create command pool");
    }

    deletion_stack.push([this]() {
      vkDestroyCommandPool(this->device, this->command_pool, nullptr);
    });
  }

  void create_command_buffers() {
    const VkCommandBufferAllocateInfo command_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    command_buffers.push_back(VK_NULL_HANDLE);
    // get command buffer
    if (vkAllocateCommandBuffers(device, &command_buffer_info,
                                 command_buffers.data()) != VK_SUCCESS) {
      throw runtime_error("failed to allocate command buffer");
    }

    deletion_stack.push([this]() {
      vkFreeCommandBuffers(this->device, this->command_pool, 1,
                           this->command_buffers.data());
    });
  }

  void create_semaphores() {
    const VkSemaphoreCreateInfo signaled_semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    const VkSemaphoreCreateInfo unsignaled_semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    vkCreateSemaphore(device, &unsignaled_semaphore_info, nullptr,
                      &semaphores.swapchain_image_is_available);

    vkCreateSemaphore(device, &unsignaled_semaphore_info, nullptr,
                      &semaphores.rendering_is_complete);

    deletion_stack.push([this]() {
      vkDestroySemaphore(device, semaphores.swapchain_image_is_available,
                         nullptr);
      vkDestroySemaphore(device, semaphores.rendering_is_complete, nullptr);
    });
  }

  void create_fences() {
    const VkFenceCreateInfo signaled_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    const VkFenceCreateInfo unsignaled_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    vkCreateFence(device, &signaled_fence_info, nullptr,
                  &fences.command_buffer_can_be_used);

    vkCreateFence(device, &unsignaled_fence_info, nullptr,
                  &fences.rendering_is_complete);

    deletion_stack.push([this]() {
      vkDestroyFence(device, fences.rendering_is_complete, nullptr);
      vkDestroyFence(device, fences.command_buffer_can_be_used, nullptr);
    });
  }

  void create_queue() {
    QueueFamilyIndex queue_family_index = find_queue_family_index();
    vkGetDeviceQueue(device, queue_family_index.draw_and_present_family.value(),
                     0, &queue);
  }

  void create_pipeline() {
    pipelines = pipeline_constructor.create(device, swapchain_image_extent,
                                            render_pass);
    deletion_stack.push([this]() {
      for (auto& pipeline : this->pipelines) {
        vkDestroyPipeline(device, pipeline, nullptr);
      };
      pipeline_constructor.deletion_stack.flush();
    });
  }

  void create_framebuffers() {
    swapchain_framebuffers.resize(swapchain_image_views.size());

    for (size_t i = 0; i < swapchain_image_views.size(); i++) {
      VkImageView attachments[] = {swapchain_image_views[i], depth_buffer_view};

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = render_pass;
      framebufferInfo.attachmentCount = 2;
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = swapchain_image_extent.width;
      framebufferInfo.height = swapchain_image_extent.height;

      // println("{}x{}", framebufferInfo.width, framebufferInfo.height);

      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                              &swapchain_framebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
      }
      deletion_stack.push([=, this]() {
        vkDestroyFramebuffer(device, swapchain_framebuffers[i], nullptr);
      });
    }
  }

  void create_allocator() {
    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical_device;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
    deletion_stack.push([this]() { vmaDestroyAllocator(this->_allocator); });
  }

  void init_vulkan() {
    deletion_stack.init();
    create_instance();
    setup_debug_messenger();
    choose_physical_device();
    create_surface();

    // and the queue as well
    create_logical_device();
    create_allocator();
    create_swapchain();

    create_depth_buffer();
    create_render_pass();

    // needed in the render pass*
    // * assuming no dynamic rendering
    create_image_views();
    create_depth_buffer_view();
    create_framebuffers();
    create_pipeline();

    // can be created anytime after device is created
    create_command_pool();
    create_command_buffers();
    // synchronization stuff
    create_fences();
    create_semaphores();
    create_queue();
    // dbg_get_surface_output_formats();
  }
  void main_loop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      // println("{}", frame_count);

      // get command buffer
      VkCommandBuffer command_buffer = command_buffers[0];

      vkWaitForFences(device, 1, &fences.command_buffer_can_be_used, VK_TRUE,
                      UINT64_MAX);
      vkResetFences(device, 1, &fences.command_buffer_can_be_used);

      vkResetCommandBuffer(command_buffer, 0);

      const VkCommandBufferBeginInfo command_buffer_begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          // .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      VkAcquireNextImageInfoKHR next_image_info = {
          .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
          .swapchain = swapchain,
          // wait 1 second maxinum
          .timeout = UINT64_MAX,
          .semaphore = semaphores.swapchain_image_is_available,
          .fence = VK_NULL_HANDLE,
          .deviceMask = static_cast<u32>(1) << physical_device_index};

      u32 swapchain_image_index;
      VK_CHECK_CONDITIONAL(vkAcquireNextImage2KHR(device, &next_image_info,
                                                  &swapchain_image_index),
                           "unable to acquire next image", {VK_SUBOPTIMAL_KHR});

      vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

      // THIS IS WHERE THE MAGIC HAPPENS !!
      // Begin Render Pass
      const vector<VkClearValue> clear_values = {
          {.color = {0.0f, 0.0f, 0.0f, 0.0f}},
          {.depthStencil = {0., 0}},
      };

      const VkRenderPassBeginInfo renderpassInfo = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .renderPass = render_pass,
          .framebuffer = swapchain_framebuffers[swapchain_image_index],
          .renderArea = {.offset = {0, 0}, .extent = swapchain_image_extent},
          .clearValueCount = static_cast<u32>(clear_values.size()),
          .pClearValues = clear_values.data(),
      };
      vkCmdBeginRenderPass(command_buffer, &renderpassInfo,
                           VK_SUBPASS_CONTENTS_INLINE);

      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipelines[0]);

      const VkViewport viewport = {
          .x = 0.0f,
          .y = 0.0f,
          .width = static_cast<float>(swapchain_image_extent.width),
          .height = static_cast<float>(swapchain_image_extent.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f};
      vkCmdSetViewport(command_buffer, 0, 1, &viewport);
      const VkRect2D scissor = {
          .offset = {0, 0},
          .extent = swapchain_image_extent,
      };
      vkCmdSetScissor(command_buffer, 0, 1, &scissor);
      vkCmdDraw(command_buffer, 3, 1, 0, 0);
      vkCmdEndRenderPass(command_buffer);
      VK_CHECK(vkEndCommandBuffer(command_buffer),
               "failed to end command buffer");

      const VkPipelineStageFlags wait_destination_stage_masks[] = {
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

      const VkSubmitInfo submit_info = {
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &semaphores.swapchain_image_is_available,
          .pWaitDstStageMask = &wait_destination_stage_masks[0],
          .commandBufferCount = 1,
          .pCommandBuffers = &command_buffer,
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = &semaphores.rendering_is_complete};

      VK_CHECK(vkQueueSubmit(queue, 1, &submit_info,
                             fences.command_buffer_can_be_used),
               "failed to submit queue");
      ;

      VkResult present_result;
      const VkPresentInfoKHR present_info = {
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &semaphores.rendering_is_complete,
          .swapchainCount = 1,
          .pSwapchains = &swapchain,
          .pImageIndices = &swapchain_image_index,
          .pResults = &present_result};
      vkQueuePresentKHR(queue, &present_info);
      // VK_CHECK(present_result, "failed to present");
      frame_count += 1;
    }
  }
  void destroy_debug_messenger() {
    if (!enableValidationLayers) {
      return;
    }
    // debug messenger must exist
    DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
  }
  void cleanup() {
    // wait until last semaphore runs
    vkWaitForFences(device, 1, &fences.command_buffer_can_be_used, VK_TRUE,
                    UINT64_MAX);

    deletion_stack.flush();

    glfwDestroyWindow(window);
    glfwTerminate();
  }
};

int main() {
  App app;
  try {
    app.run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
