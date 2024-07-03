#pragma once

#include "lib.hpp"

struct Pipeline {
 public:
  vector<VkPipelineShaderStageCreateInfo> shader_stages;
  vector<VkDynamicState> dynamic_states;
  VkPipelineDynamicStateCreateInfo dynamic_state;
  VkPipelineVertexInputStateCreateInfo vertex_input_info;
  VkPipelineInputAssemblyStateCreateInfo inputAssembly;
  VkPipelineLayout pipelineLayout;
  DeletionStack deletion_stack;
  vector<VkVertexInputBindingDescription> vertex_binding_descriptions;
  vector<VkVertexInputAttributeDescription> vertex_attribute_descriptions;

  path get_current_working_dir();
  optional<string> read_to_string(path p);
  // function basically copied from
  // https://github.com/google/shaderc/blob/main/examples/online-compile/main.cc
  optional<vector<u32>> get_spirv_from_glsl(const std::string& source_code,
                                            shaderc_shader_kind kind,
                                            const std::string& source_path,
                                            bool optimize = false);

  optional<VkShaderModule> get_compiled_shader_module(
      string shader_name,
      shaderc_shader_kind shader_kind,
      VkDevice device);

  void create_shader_stages(VkDevice device);

  void create_dynamic_state();

  void create_vertex_input_info();

  void create_input_assembly();
  vector<VkPipeline> create(VkDevice& device,
                            SwapchainDimensions& swapchain_dimensions,
                            VkRenderPass& render_pass);
};