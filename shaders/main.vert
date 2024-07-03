#version 460
#pragma shaderc_vertex_shader
#extension GL_KHR_vulkan_glsl : enable
#pragma shader_stage(vertex)

layout(location = 0) in vec3 coord;
layout(location = 1) in vec3 color;
layout(location = 0) out vec3 frag_color;

void main() {
  gl_Position = vec4(coord.xyz, 1.);
  frag_color = color;
}