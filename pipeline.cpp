#include "pipeline.hpp"
#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include "lib.hpp"
#include "vulkan/vulkan_core.h"

path Pipeline::get_current_working_dir() {
  // println("{}", current_path().string());
#ifdef NDEBUG
  // release mode code
  return current_path();
#else
  // debug mode code
  return current_path().parent_path();
#endif
}

optional<string> Pipeline::read_to_string(path p) {
  string output;

  fstream in;
  in.open(p, std::ios::in | ios::binary);
  if (in.is_open()) {
    output.resize(file_size(p));
    in.seekg(ios::beg);
    in.read(&output[0], output.size());
    in.close();
  } else {
    println("failed to open file for reading {}", p.string());
    return {};
  }
  return output;
}

// function basically copied from
// https://github.com/google/shaderc/blob/main/examples/online-compile/main.cc
optional<vector<u32>> Pipeline::get_spirv_from_glsl(
    const std::string& source_code,
    shaderc_shader_kind kind,
    const std::string& source_path,
    bool optimize) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;

  // optimize the compiled shader binary if needed
  if (optimize) {
    options.SetOptimizationLevel(shaderc_optimization_level_size);
  }

  // no idea why you need to include both the source code and the source path
  // at the same time, maybe it's for debugging?
  shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
      source_code, kind, source_path.c_str(), options);

  if (result.GetCompilationStatus() == shaderc_compilation_status_success) {
    // creates a new vector from an "initializer list", which is basically an
    // iterator costs a copy, wonder if you can do it in place instead?
    vector<u32> output = {result.cbegin(), result.cend()};
    return output;
  } else {
    // Handle compilation error
    println("failed to compile shader file `{}`, here is error message: ",
            source_path);
    cerr << "```" << endl;
    cout << result.GetErrorMessage();
    cerr << "```" << endl;
    cerr << "(If the error message was empty, make sure shader stages are "
            "defined in shader)"
         << endl;
    return {};
  }
}
optional<VkShaderModule> Pipeline::get_compiled_shader_module(
    string shader_name,
    shaderc_shader_kind shader_kind,
    VkDevice device) {
  // get shader_name from shaders folder
  path source_path = get_current_working_dir() / "shaders" / shader_name;
  optional<string> source_code = read_to_string(source_path);

  if (!source_code.has_value()) {
    println("failed to read source code from {}", shader_name);
    return {};
  }

  optional<vector<u32>> spirv = get_spirv_from_glsl(
      source_code.value(), shader_kind, source_path.string());

  if (!spirv.has_value()) {
    println("unable to compile spirv for {}", shader_name);
    return {};
  }

  VkShaderModuleCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      // sizeof(u32) is 4
      .codeSize = spirv.value().size() * sizeof(u32),
      .pCode = spirv.value().data()};

  VkShaderModule module;
  vkCreateShaderModule(device, &create_info, nullptr, &module);
  return module;
}

void Pipeline::create_shader_stages(VkDevice device) {
  optional<VkShaderModule> vert_module = get_compiled_shader_module(
      "main.vert", shaderc_glsl_infer_from_source, device);
  if (!vert_module.has_value()) {
    throw runtime_error("unable to create vertex shader module");
  }

  optional<VkShaderModule> frag_module = get_compiled_shader_module(
      "main.frag", shaderc_glsl_infer_from_source, device);

  if (!frag_module.has_value()) {
    throw runtime_error("unable to create fragment shader module");
  }

  vector<VkPipelineShaderStageCreateInfo> shader_stages = {
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,
          .module = vert_module.value(),
          .pName = "main",
      },
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = frag_module.value(),
          .pName = "main",
      },
  };
  this->shader_stages = shader_stages;
}
void Pipeline::create_dynamic_state() {
  this->dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  this->dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
      .pDynamicStates = this->dynamic_states.data()};
}
void Pipeline::create_vertex_input_info() {
  vertex_binding_descriptions = {
      {.binding = 0,
       .stride = sizeof(Vertex),
       .inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX},
  };

  vertex_attribute_descriptions = {
      // vertex
      {.location = 0,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, coord)},
      // color
      {.location = 1,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, color)},
  };

  vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount =
          static_cast<u32>(vertex_binding_descriptions.size()),
      .pVertexBindingDescriptions = vertex_binding_descriptions.data(),
      .vertexAttributeDescriptionCount =
          static_cast<u32>(vertex_attribute_descriptions.size()),
      .pVertexAttributeDescriptions = vertex_attribute_descriptions.data(),
  };
}

void Pipeline::create_input_assembly() {
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };
  this->inputAssembly = inputAssembly;
}

vector<VkPipeline> Pipeline::create(VkDevice& device,
                                    SwapchainDimensions& swapchain_dimensions,
                                    VkRenderPass& render_pass) {
  create_shader_stages(device);
  create_dynamic_state();
  create_vertex_input_info();

  // topology, or lack thereof
  create_input_assembly();

  // # viewport & scissors
  VkViewport viewport = {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float)swapchain_dimensions.extent.width,
      .height = (float)swapchain_dimensions.extent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  VkPipelineViewportStateCreateInfo viewportState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };
  VkRect2D scissor = {
      .offset = {0, 0},
      .extent = swapchain_dimensions.extent,
  };

  // # rasterizer
  VkPipelineRasterizationStateCreateInfo rasterizer{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = VK_FALSE,
      // depth bias apparently used for shadow mapping
      .depthBiasConstantFactor = VK_FALSE,
      .lineWidth = 1.0f,
  };
  // # multisampling
  VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .sampleShadingEnable = VK_FALSE,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .minSampleShading = 1.0f,           // Optional
      .pSampleMask = nullptr,             // Optional
      .alphaToCoverageEnable = VK_FALSE,  // Optional
      .alphaToOneEnable = VK_FALSE,       // Optional
  };
  VkStencilOpState stencil_op_state = {
      .failOp = VK_STENCIL_OP_KEEP,
      .passOp = VK_STENCIL_OP_KEEP,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .compareMask = 0,
      .reference = 0,
      .depthFailOp = VK_STENCIL_OP_KEEP,
      .writeMask = 0,
  };
  // # depth
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .depthBoundsTestEnable = VK_FALSE,
      .minDepthBounds = 0,
      .maxDepthBounds = 0,
      .stencilTestEnable = VK_FALSE,
      .back = stencil_op_state,
      .front = stencil_op_state,
  };

  // # blending
  VkPipelineColorBlendAttachmentState colorBlendAttachment = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,   // Optional
      .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,  // Optional
      .colorBlendOp = VK_BLEND_OP_ADD,              // Optional
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,   // Optional
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,  // Optional
      .alphaBlendOp = VK_BLEND_OP_ADD,              // Optional
  };

  // # blending
  VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,  // Optional
      .attachmentCount = 1,
      .pAttachments = &colorBlendAttachment,
      .blendConstants[0] = 0.0f,  // Optional
      .blendConstants[1] = 0.0f,  // Optional
      .blendConstants[2] = 0.0f,  // Optional
      .blendConstants[3] = 0.0f,  // Optional
  };

  // # pipeline layout (for uniforms?)
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,     // Optional
      .pSetLayouts = nullptr,  // Optional
      // push constants??
      .pushConstantRangeCount = 0,     // Optional
      .pPushConstantRanges = nullptr,  // Optional
  };

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                             &pipelineLayout) != VK_SUCCESS) {
    throw runtime_error("failed to create pipeline layout!");
  }
  deletion_stack.push([=, this]() {
    vkDestroyPipelineLayout(device, this->pipelineLayout, nullptr);
  });

  VkGraphicsPipelineCreateInfo pipeline_create_infos[] = {{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = &shader_stages[0],
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = &depth_stencil,  // Optional
      .pColorBlendState = &colorBlending,
      .pDynamicState = &dynamic_state,
      .layout = pipelineLayout,
      .renderPass = render_pass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,  // optional
      .basePipelineIndex = -1                // optional
  }};

  vector<VkPipeline> pipelines(1);

  if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipeline_create_infos[0],
                                nullptr, pipelines.data()) != VK_SUCCESS) {
    throw runtime_error("unable to create graphics pipeline");
  }

  // destroy shader modules immediately after creation, since we don't need it
  // after we initialize the pipeline
  for (auto stage : this->shader_stages) {
    vkDestroyShaderModule(device, stage.module, nullptr);
  }
  return pipelines;
}
