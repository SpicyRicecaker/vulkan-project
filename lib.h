#pragma once

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <shaderc/shaderc.hpp>

using namespace std;
using namespace fmt;
using namespace filesystem;

#define u32 uint32_t

struct Pipeline {
  VkPipelineShaderStageCreateInfo* shader_stages;

  path get_current_working_dir() {
    println("{}", current_path().string());
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
    in.open(p, ios::binary);
    if (in.is_open()) {
      output.resize(file_size(p));
      in.seekg(ios::beg);
      in.read(&output[0], output.size());
      in.close();
    } else {
      println("failed to open file for reading");
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

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module.value(),
            .pName = "Vertex",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module.value(),
            .pName = "Fragment",
        },
    };
    this->shader_stages = shader_stages;
  }

  VkPipeline* create(VkDevice device) {
    create_shader_stages(device);

    VkGraphicsPipelineCreateInfo pipeline_create_infos[] = {{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 1,
        .pStages = &shader_stages[0],
    }};

    vector<VkPipeline> pipelines(1);

    vkCreateGraphicsPipelines(device, nullptr, 1, &pipeline_create_infos[0],
                              nullptr, pipelines.data());
  }
};
