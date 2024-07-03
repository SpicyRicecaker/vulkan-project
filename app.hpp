#pragma once

#include "lib.hpp"
#include "pipeline.hpp"

class App {
 public:
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

  struct VertexBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
  };

  struct QueueFamilyIndex {
    std::optional<u32> draw_and_present_family;
    bool isComplete() { return draw_and_present_family.has_value(); };
  };

  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    vector<VkSurfaceFormatKHR> formats;
    vector<VkPresentModeKHR> presentModes;
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
    VmaAllocator allocator;
    DepthBuffer depth_b;
    vector<Vertex> vertices;
    VertexBuffer vertex_buffer;
    //
    DeletionStack deletion_stack;
    VkDebugUtilsMessengerEXT debug_messenger;
    u32 current_frame = 0;
  };

  GLFWwindow* window;
  int window_width;
  int window_height;
  int framebuffer_width;
  int framebuffer_height;
  u32 total_frames_rendered = 0;
  Context cx;

  void run();
  static void framebuffer_size_callback(GLFWwindow* window,
                                        int new_width,
                                        int new_height);
  static void window_size_callback(GLFWwindow* window,
                                   int new_width,
                                   int new_height);
  void initialize_event_listeners(Context& cx);
  void init_window(Context& cx);
  void init_game(Context& cx);
  static VkBool32 debug_messenger_callback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
      void* pUserData);
  bool layers_exists(vector<const char*>* requested_layers);
  void dbg_get_available_instance_extensions();
  vector<const char*> get_required_instance_extensions();
  void create_instance(Context& cx);
  static VkResult CreateDebugUtilsMessengerEXT(
      VkInstance instance,
      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
      const VkAllocationCallbacks* pAllocator,
      VkDebugUtilsMessengerEXT* pDebugMessenger);
  void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                     VkDebugUtilsMessengerEXT messenger,
                                     const VkAllocationCallbacks* pAllocator);
  VkDebugUtilsMessengerCreateInfoEXT get_debug_messenger_info();
  void setup_debug_messenger();
  void choose_physical_device(Context& cx);
  void dbg_get_surface_output_formats(Context& cx);
  QueueFamilyIndex find_queue_family_index(Context& cx);
  bool device_includes_extensions();
  bool is_device_suitable(Context& cx);
  void create_logical_device(Context& cx);
  SwapChainSupportDetails get_swapchain_support();
  void create_surface(Context& cx);
  void create_swapchain(Context& cx);
  void create_depth_buffer(Context& cx);
  void create_vertex_buffer(Context& cx);
  void create_render_pass(Context& cx);
  VkImageView create_image_view(VkImage image,
                                VkFormat format,
                                VkImageAspectFlags flags);
  void create_depth_buffer_view(Context& cx);
  void create_image_views(Context& ctx);
  void create_command_pool(Context& cx);
  void create_command_buffers(Context& cx);
  void create_semaphores(Context& cx);
  void create_fences(Context& cx);
  void create_queue(Context& cx);
  void teardown_framebuffers(Context& cx);
  void teardown_depth_buffer(Context& cx);
  void teardown_swapchain_and_image_views(Context& cx);
  void recreate_swapchain(Context& cx);
  void create_pipeline(Context& cx);
  void create_framebuffers(Context& cx);
  void create_allocator();
  void init_vulkan(Context& cx);
  void render_frame(Context& cx);
  void main_loop();
  void destroy_debug_messenger(Context& cx);
  void teardown();
};