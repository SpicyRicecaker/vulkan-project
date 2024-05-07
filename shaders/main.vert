#version 460
#pragma shaderc_vertex_shader
#extension GL_KHR_vulkan_glsl : enable

#pragma shader_stage(vertex)
void main() {
  vec3 triangle_vertices[3] = {
    vec3(-.5, -.5, 1.),
    vec3(.5, -.5, 1.),
    vec3(0., .5, 1.),
  };
 
  gl_Position = vec4(triangle_vertices[gl_VertexIndex].xyz, 1.);
}