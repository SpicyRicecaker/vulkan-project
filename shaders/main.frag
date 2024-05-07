#version 460
#pragma shaderc_fragment_shader

#pragma shader_stage(fragment)
layout(location = 0) out vec4 color;

void main() {
  color = vec4(1., 0., 0., 0.);
}