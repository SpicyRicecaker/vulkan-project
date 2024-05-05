# version 460

void main() {
  vec3 triangle_vertices[3] = {
    vec3(-.5, -.5, 1.),
    vec3(.5, -.5, 1.),
    vec3(0., .5, 1.),
  };

  gl_Position = vec4(triangle_vertices[gl_VertexID].xyz, 1.);
}