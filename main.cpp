#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

// api for glsl -> spirv conversion
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <shaderc/shaderc.hpp>

// might need to change this include dir based on OS
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "lib.h"

using namespace std;
using namespace fmt;
using namespace filesystem;

#define u32 uint32_t
#define u64 uint64_t

const u32 WIDTH = 800;
const u32 HEIGHT = 600;

#ifdef __APPLE__
const char* os = "macos";
#elif _WIN32
const char* os = "windows";
#endif

const vector<const char*> wanted_device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    // unneeded on vulkan 1.3, see
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/enabling_buffer_device_address.html
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

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

const u32 MAX_IN_FLIGHT_FRAMES = 2;

class App {
  // organization of struct members
  // greatly inspired by vulkan samples by ARM developers
  struct SwapchainDimensions {
    VkColorSpaceKHR colorspace;
    VkExtent2D extent;
    VkFormat format;
  };

  struct Semaphores {
    vector<VkSemaphore> swapchain_image_is_available;
    vector<VkSemaphore> rendering_is_complete;
  };

  struct Fences {
    vector<VkFence> command_buffer_can_be_used;
    vector<VkFence> rendering_is_complete;
  };

  struct DepthBuffer {
    VkImage image;
    VkImageView image_view;
    VkFormat format;
    VmaAllocation allocation;
  };

  // Vulkan objects and global state
  struct Context {
    VkInstance instance = VK_NULL_HANDLE;
    u32 physical_device_index = 0;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    SwapchainDimensions swapchain_dimensions;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    // per-frame
    vector<VkImage> swapchain_images;
    // per-frame
    vector<VkImageView> swapchain_image_views;
    // per-frame
    vector<VkFramebuffer> swapchain_framebuffers;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    // per-frame
    vector<VkCommandBuffer> command_buffers;
    VkQueue queue = VK_NULL_HANDLE;
    Pipeline pipeline_constructor;
    vector<VkPipeline> pipelines = {VK_NULL_HANDLE};
    Semaphores semaphores;
    Fences fences;

    //
    VmaAllocator _allocator;
    DepthBuffer depth_b;
    //
    DeletionStack deletion_stack;
    VkDebugUtilsMessengerEXT debug_messenger;
    u32 current_frame = 0;
  };

 public:
  GLFWwindow* window;
  int window_width;
  int window_height;
  int framebuffer_width;
  int framebuffer_height;
  u32 total_frames_rendered = 0;
  Context cx;

  void run() {
    init_window();
    init_vulkan(cx);
    main_loop();
    teardown();
  }

 private:
  void init_window() {
    glfwInit();

    // disable opengl context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Window", nullptr, nullptr);

    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
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

  void create_instance(Context& cx) {
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

    if (vkCreateInstance(&create_info, nullptr, &cx.instance) != VK_SUCCESS) {
      throw runtime_error("failed to create instance");
    };
    cx.deletion_stack.push(
        [this]() { vkDestroyInstance(this->cx.instance, nullptr); });
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
    if (CreateDebugUtilsMessengerEXT(cx.instance, &debug_messenger_info,
                                     nullptr,
                                     &cx.debug_messenger) != VK_SUCCESS) {
      throw runtime_error("failed to create debug messenger");
    };
    cx.deletion_stack.push(
        [this]() { this->destroy_debug_messenger(this->cx); });
  }

  void choose_physical_device(Context& cx) {
    u32 physical_device_count;
    vkEnumeratePhysicalDevices(cx.instance, &physical_device_count, nullptr);
    vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(cx.instance, &physical_device_count,
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
    cx.physical_device = best_physical_device;
    cx.physical_device_index = best_physical_device_index;
  }

  void dbg_get_surface_output_formats(Context& cx) {
    // debug surface properties - get output rgba formats
    VkPhysicalDeviceSurfaceInfo2KHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .surface = cx.surface};

    // get supported formats
    u32 surface_formats_count;
    vkGetPhysicalDeviceSurfaceFormats2KHR(cx.physical_device, &surface_info,
                                          &surface_formats_count, nullptr);

    vector<VkSurfaceFormat2KHR> surface_formats(surface_formats_count);
    vkGetPhysicalDeviceSurfaceFormats2KHR(cx.physical_device, &surface_info,
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

  QueueFamilyIndex find_queue_family_index(Context& cx) {
    // get queue device capabilities from physical device??
    u32 queue_family_property_count;
    vkGetPhysicalDeviceQueueFamilyProperties(
        cx.physical_device, &queue_family_property_count, nullptr);
    vector<VkQueueFamilyProperties> queue_family_properties(
        queue_family_property_count);
    vkGetPhysicalDeviceQueueFamilyProperties(cx.physical_device,
                                             &queue_family_property_count,
                                             queue_family_properties.data());

    std::optional<u32> draw_and_present_family;

    int i = 0;
    for (auto& p : queue_family_properties) {
      if ((p.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(cx.physical_device, i, cx.surface,
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
    vkEnumerateDeviceExtensionProperties(cx.physical_device, nullptr,
                                         &device_property_count, nullptr);
    std::vector<VkExtensionProperties> device_extensions(device_property_count);
    vkEnumerateDeviceExtensionProperties(cx.physical_device, nullptr,
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

  bool is_device_suitable(Context& cx) {
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
      vkGetPhysicalDeviceFeatures2(cx.physical_device, &device_features);

      if (features.bufferDeviceAddress == VK_TRUE) {
        buffer_device_address_support = true;
      }
    }

    QueueFamilyIndex queue_family_index = find_queue_family_index(cx);

    bool swapchain_support = false;

    // Check if device supports swapchain extension
    return queue_family_index.isComplete() && device_includes_extensions();
  }

  void create_logical_device(Context& cx) {
    if (!is_device_suitable(cx)) {
      throw runtime_error("device not suitable for various reasons");
    }

    QueueFamilyIndex queue_family_index = find_queue_family_index(cx);

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

    if (vkCreateDevice(cx.physical_device, &device_create_info, nullptr,
                       &cx.device) != VK_SUCCESS) {
      throw runtime_error("unable to create device");
    };
    cx.deletion_stack.push(
        [this]() { vkDestroyDevice(this->cx.device, nullptr); });

    // queues can be requested later based on the logical device
    VkQueue queue;
    vkGetDeviceQueue(cx.device,
                     queue_family_index.draw_and_present_family.value(), 0,
                     &queue);
  }

  void create_surface(Context& cx) {
    if (glfwCreateWindowSurface(cx.instance, window, nullptr, &cx.surface) !=
        VK_SUCCESS) {
      throw runtime_error("unable to create surface");
    };
    cx.deletion_stack.push([this]() {
      vkDestroySurfaceKHR(this->cx.instance, this->cx.surface, nullptr);
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
        cx.physical_device, cx.surface,
        &swapchain_support_details.capabilities);

    u32 surface_formats_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(cx.physical_device, cx.surface,
                                         &surface_formats_count, nullptr);
    swapchain_support_details.formats.resize(surface_formats_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        cx.physical_device, cx.surface, &surface_formats_count,
        swapchain_support_details.formats.data());

    u32 present_modes_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(cx.physical_device, cx.surface,
                                              &present_modes_count, nullptr);
    swapchain_support_details.presentModes.resize(present_modes_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        cx.physical_device, cx.surface, &present_modes_count,
        swapchain_support_details.presentModes.data());

    return swapchain_support_details;
  }

  // creates a swapchain, from an old one, if possible
  void create_swapchain(Context& cx) {
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

    if (!format_valid) {
      // cout << get_swapchain_support() << endl;
      throw runtime_error("swapchain doesn't have supported formats");
    }

    u32 min_image_count =
        swapchain_support_details.capabilities.maxImageCount == 0
            ? swapchain_support_details.capabilities.minImageCount + 1
            : min(swapchain_support_details.capabilities.minImageCount + 1,
                  swapchain_support_details.capabilities.maxImageCount);

    // account for high-dpi scaling of some systems:
    // the surface resolution may be greater or lower than the window
    // dimensions. Only the framebuffer matches up.

    cx.swapchain_dimensions.extent = {
        .width = static_cast<u32>(framebuffer_width),
        .height = static_cast<u32>(framebuffer_height)};
    // println("dbg: {}x{}", window_width, window_height);

    QueueFamilyIndex index = find_queue_family_index(cx);

    VkSwapchainKHR old_swapchain = cx.swapchain;

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = cx.surface,
        .minImageCount = min_image_count,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = cx.swapchain_dimensions.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        // only good if we have one queue writing to the swapchain??
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        // .queueFamilyIndexCount = 1,
        // .pQueueFamilyIndices = nullptr,
        .preTransform = swapchain_support_details.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain};

    VK_CHECK(vkCreateSwapchainKHR(cx.device, &swapchain_create_info, nullptr,
                                  &cx.swapchain),
             "failed to create swapchain");

    // after new swapchain is created, all resources can be freed
    // related to the old swapchain
    if (old_swapchain != VK_NULL_HANDLE) {
      for (auto& image_view : cx.swapchain_image_views) {
        vkDestroyImageView(cx.device, image_view, nullptr);
      }

      vkDestroySwapchainKHR(cx.device, old_swapchain, nullptr);
    }

    u32 image_count;
    vkGetSwapchainImagesKHR(cx.device, cx.swapchain, &image_count, nullptr);
    cx.swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(cx.device, cx.swapchain, &image_count,
                            cx.swapchain_images.data());
    cx.swapchain_dimensions.format = swapchain_create_info.imageFormat;
    cx.swapchain_dimensions.colorspace = swapchain_create_info.imageColorSpace;
  }

  void create_depth_buffer(Context& cx) {
    cx.depth_b.format = VK_FORMAT_D32_SFLOAT;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = cx.depth_b.format,
        .extent = {.width = static_cast<u32>(framebuffer_width),
                   .height = static_cast<u32>(framebuffer_height),
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
    if (vmaCreateImage(cx._allocator, &image_info, &image_alloc_info,
                       &cx.depth_b.image, &cx.depth_b.allocation,
                       nullptr) != VK_SUCCESS) {
      cerr << "error creating depth image" << endl;
    };

    cx.deletion_stack.push([this]() {
      vmaDestroyImage(this->cx._allocator, this->cx.depth_b.image,
                      this->cx.depth_b.allocation);
    });
  }

  void create_render_pass(Context& cx) {
    vector<VkAttachmentDescription> attachments = {
        // color
        {
            .format = cx.swapchain_dimensions.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        // depth
        {.format = cx.depth_b.format,
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

    if (vkCreateRenderPass(cx.device, &render_pass_info, nullptr,
                           &cx.render_pass) != VK_SUCCESS) {
      throw runtime_error("failed to create render pass");
    };
    cx.deletion_stack.push([this]() {
      vkDestroyRenderPass(this->cx.device, this->cx.render_pass, nullptr);
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
    if (vkCreateImageView(cx.device, &image_view_info, nullptr, &image_view) !=
        VK_SUCCESS) {
      throw runtime_error("failed to create image view!");
    }

    return image_view;
  }

  void create_depth_buffer_view(Context& cx) {
    cx.depth_b.image_view = create_image_view(
        cx.depth_b.image, cx.depth_b.format, VK_IMAGE_ASPECT_DEPTH_BIT);
    cx.deletion_stack.push([this]() {
      vkDestroyImageView(this->cx.device, this->cx.depth_b.image_view, nullptr);
    });
  }

  // image views used at runtime during pipeline rendering
  void create_image_views(Context& ctx) {
    cx.swapchain_image_views.resize(cx.swapchain_images.size());
    auto i = 0;
    for (auto swapchain_image : cx.swapchain_images) {
      cx.swapchain_image_views[i] =
          create_image_view(swapchain_image, cx.swapchain_dimensions.format,
                            VK_IMAGE_ASPECT_COLOR_BIT);
      i++;
    }
  }

  void create_command_pool(Context& cx) {
    QueueFamilyIndex queue_family_index = find_queue_family_index(cx);

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index.draw_and_present_family.value()};
    ;

    if (vkCreateCommandPool(cx.device, &command_pool_create_info, nullptr,
                            &cx.command_pool) != VK_SUCCESS) {
      throw runtime_error("failed to create command pool");
    }

    cx.deletion_stack.push([this]() {
      vkDestroyCommandPool(this->cx.device, this->cx.command_pool, nullptr);
    });
  }

  void create_command_buffers(Context& cx) {
    const VkCommandBufferAllocateInfo command_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cx.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_IN_FLIGHT_FRAMES,
    };
    
    cx.command_buffers.resize(MAX_IN_FLIGHT_FRAMES);

    // get command buffer
    VK_CHECK(vkAllocateCommandBuffers(cx.device, &command_buffer_info,
                                      cx.command_buffers.data()),
             "failed to allocate command buffers");

    cx.deletion_stack.push([this]() {
      vkFreeCommandBuffers(this->cx.device, this->cx.command_pool, 1,
                           this->cx.command_buffers.data());
    });
  }

  void create_semaphores(Context& cx) {
    const VkSemaphoreCreateInfo unsignaled_semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    cx.semaphores.swapchain_image_is_available.resize(MAX_IN_FLIGHT_FRAMES);
    cx.semaphores.rendering_is_complete.resize(MAX_IN_FLIGHT_FRAMES);

    for (int i = 0; i < MAX_IN_FLIGHT_FRAMES; i++) {
      vkCreateSemaphore(cx.device, &unsignaled_semaphore_info, nullptr,
                        &cx.semaphores.swapchain_image_is_available[i]);

      vkCreateSemaphore(cx.device, &unsignaled_semaphore_info, nullptr,
                        &cx.semaphores.rendering_is_complete[i]);

      cx.deletion_stack.push([i, this]() {
        vkDestroySemaphore(this->cx.device,
                           this->cx.semaphores.swapchain_image_is_available[i],
                           nullptr);
        vkDestroySemaphore(this->cx.device,
                           this->cx.semaphores.rendering_is_complete[i],
                           nullptr);
      });
    }
  }

  void create_fences(Context& cx) {
    const VkFenceCreateInfo signaled_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    const VkFenceCreateInfo unsignaled_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    cx.fences.command_buffer_can_be_used.resize(MAX_IN_FLIGHT_FRAMES);
    cx.fences.rendering_is_complete.resize(MAX_IN_FLIGHT_FRAMES);

    for (int i = 0; i < MAX_IN_FLIGHT_FRAMES; i++) {
      vkCreateFence(cx.device, &signaled_fence_info, nullptr,
                    &cx.fences.command_buffer_can_be_used[i]);

      vkCreateFence(cx.device, &unsignaled_fence_info, nullptr,
                    &cx.fences.rendering_is_complete[i]);

      cx.deletion_stack.push([i, this]() {
        vkDestroyFence(this->cx.device,
                       this->cx.fences.rendering_is_complete[i], nullptr);
        vkDestroyFence(this->cx.device,
                       this->cx.fences.command_buffer_can_be_used[i], nullptr);
      });
    }
  }

  void create_queue(Context& cx) {
    QueueFamilyIndex queue_family_index = find_queue_family_index(cx);
    vkGetDeviceQueue(cx.device,
                     queue_family_index.draw_and_present_family.value(), 0,
                     &cx.queue);
  }

  void teardown_framebuffers(Context& cx) {
    // since in create swapchain the destroy swapchain is already enqueued
    for (auto& swapchain_framebuffer : cx.swapchain_framebuffers) {
      vkDestroyFramebuffer(cx.device, swapchain_framebuffer, nullptr);
    }
  }

  void teardown_swapchain_and_image_views(Context& cx) {
    for (auto& swapchain_image_view : cx.swapchain_image_views) {
      vkDestroyImageView(cx.device, swapchain_image_view, nullptr);
    }

    vkDestroySwapchainKHR(cx.device, cx.swapchain, nullptr);
  }

  void recreate_swapchain(Context& cx) {
    VK_CHECK(vkDeviceWaitIdle(cx.device), "failed to wait for device");

    teardown_framebuffers(cx);

    create_swapchain(cx);
    create_image_views(cx);
    create_framebuffers(cx);
  }

  void create_pipeline(Context& cx) {
    cx.pipelines = cx.pipeline_constructor.create(
        cx.device, cx.swapchain_dimensions.extent, cx.render_pass);
    cx.deletion_stack.push([this]() {
      for (auto& pipeline : this->cx.pipelines) {
        vkDestroyPipeline(this->cx.device, pipeline, nullptr);
      };
      this->cx.pipeline_constructor.deletion_stack.flush();
    });
  }

  void create_framebuffers(Context& cx) {
    cx.swapchain_framebuffers.resize(cx.swapchain_image_views.size());

    for (size_t i = 0; i < cx.swapchain_image_views.size(); i++) {
      VkImageView attachments[] = {cx.swapchain_image_views[i], cx.depth_b.image_view};

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = cx.render_pass;
      framebufferInfo.attachmentCount = 2;
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = cx.swapchain_dimensions.extent.width;
      framebufferInfo.height = cx.swapchain_dimensions.extent.height;

      // println("{}x{}", framebufferInfo.width, framebufferInfo.height);

      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(cx.device, &framebufferInfo, nullptr,
                              &cx.swapchain_framebuffers[i]) != VK_SUCCESS) {
        throw runtime_error("failed to create framebuffer!");
      }
    }
  }

  void create_allocator() {
    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = cx.physical_device;
    allocatorInfo.device = cx.device;
    allocatorInfo.instance = cx.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &cx._allocator);
    cx.deletion_stack.push([this]() { vmaDestroyAllocator(this->cx._allocator); });
  }

  void init_vulkan(Context& cx) {
    cx.deletion_stack.init();
    create_instance(cx);
    setup_debug_messenger();
    choose_physical_device(cx);
    create_surface(cx);

    // and the queue as well
    create_logical_device(cx);
    create_allocator();
    create_swapchain(cx);

    create_depth_buffer(cx);
    create_render_pass(cx);

    // needed in the render pass*
    // * assuming no dynamic rendering
    create_image_views(cx);
    create_depth_buffer_view(cx);
    create_framebuffers(cx);
    create_pipeline(cx);

    // can be created anytime after device is created
    create_command_pool(cx);
    create_command_buffers(cx);
    // synchronization stuff
    create_fences(cx);
    create_semaphores(cx);
    create_queue(cx);
    // dbg_get_surface_output_formats();
  }
  void render_frame(Context& cx) {
    // println("-----------------{}----------------", total_frames_rendered);
    vkWaitForFences(cx.device, 1,
                    &cx.fences.command_buffer_can_be_used[cx.current_frame], VK_TRUE,
                    UINT64_MAX);

    VkAcquireNextImageInfoKHR next_image_info = {
        .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
        .swapchain = cx.swapchain,
        // wait 1 second maxinum
        .timeout = UINT64_MAX,
        .semaphore = cx.semaphores.swapchain_image_is_available[cx.current_frame],
        .fence = VK_NULL_HANDLE,
        .deviceMask = static_cast<u32>(1) << cx.physical_device_index};

    u32 swapchain_image_index;
    VkResult acquire_next_image_result = vkAcquireNextImage2KHR(
        cx.device, &next_image_info, &swapchain_image_index);
    if (acquire_next_image_result == VK_ERROR_OUT_OF_DATE_KHR ||
        acquire_next_image_result == VK_SUBOPTIMAL_KHR) {
      // delete the now invalid semaphore
      vkDestroySemaphore(cx.device,
                         cx.semaphores.swapchain_image_is_available[cx.current_frame],
                         nullptr);

      const VkSemaphoreCreateInfo unsignaled_semaphore_info = {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      };

      vkCreateSemaphore(
          cx.device, &unsignaled_semaphore_info, nullptr,
          &cx.semaphores.swapchain_image_is_available[cx.current_frame]);

      // recreate the swapchain
      // println("recreating swapchain {}", current_frame);
      recreate_swapchain(cx);
      return;
    } else if (acquire_next_image_result != VK_SUCCESS) {
      throw runtime_error("unable to acquire next image");
    }

    vkResetFences(cx.device, 1, &cx.fences.command_buffer_can_be_used[cx.current_frame]);

    // get command buffer
    VkCommandBuffer command_buffer = cx.command_buffers[cx.current_frame];

    const VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        // .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkResetCommandBuffer(command_buffer, 0);

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    // THIS IS WHERE THE MAGIC HAPPENS !!
    // Begin Render Pass
    const vector<VkClearValue> clear_values = {
        {.color = {0.0f, 0.0f, 0.0f, 0.0f}},
        {.depthStencil = {0., 0}},
    };

    const VkRenderPassBeginInfo renderpassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = cx.render_pass,
        .framebuffer = cx.swapchain_framebuffers[swapchain_image_index],
        .renderArea = {.offset = {0, 0}, .extent = cx.swapchain_dimensions.extent},
        .clearValueCount = static_cast<u32>(clear_values.size()),
        .pClearValues = clear_values.data(),
    };
    vkCmdBeginRenderPass(command_buffer, &renderpassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      cx.pipelines[0]);

    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(cx.swapchain_dimensions.extent.width),
        .height = static_cast<float>(cx.swapchain_dimensions.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    const VkRect2D scissor = {
        .offset = {0, 0},
        .extent = cx.swapchain_dimensions.extent,
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
        .pWaitSemaphores =
            &cx.semaphores.swapchain_image_is_available[cx.current_frame],
        .pWaitDstStageMask = &wait_destination_stage_masks[0],
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &cx.semaphores.rendering_is_complete[cx.current_frame]};

    VK_CHECK(vkQueueSubmit(cx.queue, 1, &submit_info,
                           cx.fences.command_buffer_can_be_used[cx.current_frame]),
             "failed to submit queue");
    ;

    VkResult present_result;
    const VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &cx.semaphores.rendering_is_complete[cx.current_frame],
        .swapchainCount = 1,
        .pSwapchains = &cx.swapchain,
        .pImageIndices = &swapchain_image_index,
        .pResults = &present_result};
    vkQueuePresentKHR(cx.queue, &present_info);
    cx.current_frame = (cx.current_frame + 1) % MAX_IN_FLIGHT_FRAMES;
  }
  void main_loop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      render_frame(cx);

      // VK_CHECK(present_result, "failed to present");
      total_frames_rendered += 1;
      // DEBUG
      // if (total_frames_rendered > 3) {
      //   return;
      // }
    }
  }
  void destroy_debug_messenger(Context& cx) {
    if (!enableValidationLayers) {
      return;
    }
    // debug messenger must exist
    DestroyDebugUtilsMessengerEXT(cx.instance, cx.debug_messenger, nullptr);
  }
  void teardown() {
    // println("------------------begin cleanup---------------------");
    // wait until last semaphore/fence runs
    vkDeviceWaitIdle(cx.device);
    // since the swapchain is created and destroyed potentially many times
    // at game time, it needs to be manually tracked.
    teardown_framebuffers(cx);
    teardown_swapchain_and_image_views(cx);
    cx.deletion_stack.flush();

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
