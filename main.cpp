#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <iostream>
#include <vector>

using namespace std;

#define u32 uint32_t

const u32 WIDTH = 800;
const u32 HEIGHT = 600;

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

class App {
public:
  GLFWwindow *window;
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkPhysicalDevice physical_device;
  VkSurfaceKHR surface;
  VkDevice device;
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
  }

  // All messenger functions must have the signature
  static VkBool32 debug_messenger_callback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
      void *pUserData) {
    // if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      cerr << pCallbackData->pMessage << endl;
    }
    return VK_FALSE;
  }

  bool layers_exists(vector<const char *> *requested_layers) {
    u32 layers_count = 0;
    vkEnumerateInstanceLayerProperties(&layers_count, nullptr);
    vector<VkLayerProperties> available_layers(layers_count);
    // for some reason, you need to include the &layers_count ref here again,
    // otherwise you get a sigsev
    vkEnumerateInstanceLayerProperties(&layers_count, available_layers.data());

    // debug
    // for (auto& available_layer : available_layers) {
    //   cout << available_layer.layerName << endl;
    // }

    for (auto &requested_layer : *requested_layers) {
      bool requested_layer_exists = false;
      for (auto &available_layer : available_layers) {
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

  void dbg_get_available_extensions() {
    u32 extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    cout << extension_count << " extensions supported" << endl;

    vector<VkExtensionProperties> v_extensions(extension_count);

    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                           v_extensions.data());

    for (auto &a : v_extensions) {
      cout << a.extensionName << endl;
    }
  }

  vector<const char *> get_required_extensions() {
    // debug
    assert(glfwVulkanSupported());

    // query glfw for the extensions it needs
    u32 extensions_count;
    // guarantees the inclusion of VK_KHR_surface
    // if on MacOS, includes VK_EXT_metal_surface
    const char **t_extensions =
        glfwGetRequiredInstanceExtensions(&extensions_count);

    // debug
    // cout << "there are " << extensions_count << " required glfw extensions."
    //      << endl;

    vector<const char *> extensions;
    for (int i = 0; i < extensions_count; i++) {
      extensions.emplace_back(t_extensions[i]);
    }
    // add the khr 2 extension
    extensions.emplace_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

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

    // get required textensions
    auto extensions = get_required_extensions();

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    vector<const char *> validation_layers = {"VK_LAYER_KHRONOS_validation"};
    if (enableValidationLayers && !layers_exists(&validation_layers)) {
      throw runtime_error("validation layers requested but validation layer "
                          "does not exist on device");
    }

    // this is the beauty of cpp right here
    // ensure the VK_LAYER_KHRONOS_validation is available on the current gpu
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
  }

  VkResult CreateDebugUtilsMessengerEXT(
      VkInstance instance,
      const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
      const VkAllocationCallbacks *pAllocator,
      VkDebugUtilsMessengerEXT *pDebugMessenger) {
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
                                     const VkAllocationCallbacks *pAllocator) {
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
  }

  void choose_physical_device() {
    u32 physical_device_count;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);
    vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count,
                               physical_devices.data());

    int best_score = -1;
    VkPhysicalDevice best_physical_device = VK_NULL_HANDLE;

    for (auto l_physical_device : physical_devices) {
      VkPhysicalDeviceProperties device_properties;
      vkGetPhysicalDeviceProperties(l_physical_device, &device_properties);

      int current_score = 0;

      if (device_properties.deviceType ==
          VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        current_score += 1000;
      }

      if (current_score >= best_score) {
        best_physical_device = l_physical_device;
        best_score = current_score;
      }
    }
    this->physical_device = best_physical_device;
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

    for (auto &surface_format : surface_formats) {
      cout << surface_format.surfaceFormat.colorSpace << endl;
    }
  }
  
  void create_device() {
    VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      
      
    };
    
    vkCreateDevice(physical_device, &device_create_info, nullptr, &device);
  }

  void create_surface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
        VK_SUCCESS) {
      throw runtime_error("unable to create surface");
    };
  }
  
  void create_render_pass() {
    // vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
  }

  void create_pipeline() {
    
  }
  
  void init_vulkan() {
    // dbg_get_available_extensions();
    create_instance();
    setup_debug_messenger();
    choose_physical_device();
    create_surface();
    
    create_render_pass();
    create_pipeline();
    
    // dbg_get_surface_output_formats();
  }
  void main_loop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
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
    destroy_debug_messenger();
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
  }
};

int main() {
  App app;
  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
