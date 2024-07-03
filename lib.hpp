#pragma once

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <shaderc/shaderc.hpp>
#include <string>

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

#include <vma/vk_mem_alloc.h>

using namespace std;
using namespace fmt;
using namespace filesystem;

#define u32 uint32_t
#define u64 uint64_t

const u32 WIDTH = 800;
const u32 HEIGHT = 600;

extern const char* os;

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

const u32 MAX_IN_FLIGHT_FRAMES = 2;

using namespace std;
using namespace fmt;
using namespace filesystem;

#define u32 uint32_t

struct DeletionStack {
  vector<function<void()>> cleanup_functions;

  void init();
  // move semantics
  // since lambdas as an argument are rvalues, can take ownership instead of
  // copying
  void push(function<void()>&& fn);

  // flush from back first
  void flush();
};

void VK_CHECK_CONDITIONAL(VkResult result,
                          string err,
                          vector<VkResult> optionals);

void VK_CHECK(VkResult result, string err);

// organization of struct members
// greatly inspired by vulkan samples by ARM developers
struct SwapchainDimensions {
  VkColorSpaceKHR colorspace;
  VkExtent2D extent;
  VkFormat format;
};

struct Vertex {
  glm::vec3 coord;
  glm::vec3 color;
};
