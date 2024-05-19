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

using namespace std;
using namespace fmt;
using namespace filesystem;

#define u32 uint32_t

struct DeletionStack {
  vector<function<void()>> cleanup_functions;

  void init() {
    // shouldn't be more than this many vulkan objects at a time
    // apparently deque has less allocations and copies than vector,
    // but the implementation is really complicated
    cleanup_functions.reserve(20);
  }
  // move semantics
  // since lambdas as an argument are rvalues, can take ownership instead of
  // copying
  void push(function<void()>&& fn) { cleanup_functions.push_back(fn); }

  // flush from back first
  void flush() {
    for (auto it = cleanup_functions.rbegin(); it != cleanup_functions.rend();
         it++) {
      (*it)();
    }
    cleanup_functions.clear();
  }
};

struct Pipeline {
  vector<VkPipelineShaderStageCreateInfo> shader_stages;
  vector<VkDynamicState> dynamic_states;
  VkPipelineDynamicStateCreateInfo dynamic_state;
  VkPipelineVertexInputStateCreateInfo vertex_input_info;
  VkPipelineInputAssemblyStateCreateInfo inputAssembly;
  VkPipelineLayout pipelineLayout;
  DeletionStack deletion_stack;

  path get_current_working_dir() {
    // println("{}", current_path().string());
#ifdef NDEBUG
    // release mode code
    return current_path();
#else
    // debug mode code
    return current_path().parent_path();
#endif
  }

  optional<string> read_to_string(path p) {
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
  optional<vector<u32>> get_spirv_from_glsl(const std::string& source_code,
                                            shaderc_shader_kind kind,
                                            const std::string& source_path,
                                            bool optimize = false) {
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

  optional<VkShaderModule> get_compiled_shader_module(
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

  void create_shader_stages(VkDevice device) {
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

  void create_dynamic_state() {
    this->dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                            VK_DYNAMIC_STATE_SCISSOR};

    this->dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = this->dynamic_states.data()};
  }

  void create_vertex_input_info() {
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,  // Optional
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,  // Optional
    };
    this->vertex_input_info = vertex_input_info;
  }

  void create_input_assembly() {
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    this->inputAssembly = inputAssembly;
  }

  vector<VkPipeline> create(VkDevice device,
                            VkExtent2D swapchain_image_extent,
                            VkRenderPass render_pass) {
    create_shader_stages(device);
    create_dynamic_state();
    create_vertex_input_info();
    // topology, or lack thereof
    create_input_assembly();

    // # viewport & scissors
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchain_image_extent.width;
    viewport.height = (float)swapchain_image_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_image_extent;

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
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;           // Optional
    multisampling.pSampleMask = nullptr;             // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
    multisampling.alphaToOneEnable = VK_FALSE;       // Optional
    // # depth
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    depth_stencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.pNext = NULL;
    depth_stencil.flags = 0;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.minDepthBounds = 0;
    depth_stencil.maxDepthBounds = 0;
    depth_stencil.stencilTestEnable = VK_FALSE;
    depth_stencil.back.failOp = VK_STENCIL_OP_KEEP;
    depth_stencil.back.passOp = VK_STENCIL_OP_KEEP;
    depth_stencil.back.compareOp = VK_COMPARE_OP_ALWAYS;
    depth_stencil.back.compareMask = 0;
    depth_stencil.back.reference = 0;
    depth_stencil.back.depthFailOp = VK_STENCIL_OP_KEEP;
    depth_stencil.back.writeMask = 0;
    depth_stencil.front = depth_stencil.back;

    // # blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ZERO;                                        // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ZERO;                             // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    // # blending
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;  // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;  // Optional
    colorBlending.blendConstants[1] = 0.0f;  // Optional
    colorBlending.blendConstants[2] = 0.0f;  // Optional
    colorBlending.blendConstants[3] = 0.0f;  // Optional

    // # pipeline layout (for uniforms?)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;     // Optional
    pipelineLayoutInfo.pSetLayouts = nullptr;  // Optional
    // push constants??
    pipelineLayoutInfo.pushConstantRangeCount = 0;     // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr;  // Optional

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
    };


        for (auto stage : this->shader_stages) {
      vkDestroyShaderModule(device, stage.module, nullptr);
    }
    return pipelines;
  }
};
