#version 460
#pragma shaderc_vertex_shader
#extension GL_KHR_vulkan_glsl : enable
#pragma shader_stage(vertex)

layout(location = 0) out vec3 frag_color;

void main() {
  vec3 triangle_vertices[3] = {
    vec3(-.5, .5, 0.),
    vec3(0., -.5, 0.),
    vec3(.5, .5, 0.),
  };
  vec3 vertex_colors[3] = {
    vec3(1., 0., 0.),
    vec3(0., 1., 0.),
    vec3(0., 0., 1.),
  };
 
  gl_Position = vec4(triangle_vertices[gl_VertexIndex].xyz, 1.);
  frag_color = vertex_colors[gl_VertexIndex];
}