#version 450
// Barrique stress3d: Vulkan fragment shader
layout(binding = 0) uniform sampler2D tex;
layout(location = 0) in vec4 in_col;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec4 frag;
void main() { frag = in_col * texture( tex, in_uv ); }
