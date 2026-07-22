/* clang-format off */
#[vertex]

#version 450

#VERSION_DEFINES

/* clang-format on */

layout(push_constant, std430) uniform Params {
	mat4 mvp;
	vec4 color;
}
params;

layout(location = 0) in vec3 vertex_attrib;

void main() {
	// mvp already includes Godot's depth-correction (Y-flip + reverse-Z remap),
	// matching the projection the scene depth buffer was written with, so the
	// fragment depth-tests correctly against it. Keep real projected depth.
	gl_Position = params.mvp * vec4(vertex_attrib, 1.0);
}

/* clang-format off */
#[fragment]

#version 450

#VERSION_DEFINES

/* clang-format on */

layout(push_constant, std430) uniform Params {
	mat4 mvp;
	vec4 color;
}
params;

layout(location = 0) out vec4 frag_color;

void main() {
	// SPIKE 2 (display-space additive): output a display-space (gamma-encoded)
	// color directly -- no tonemap, no sRGB encode. The destination is the
	// post-tonemap UNORM render target, which already holds sRGB-encoded scene
	// color. With hardware BLEND_OP_ADD (src=ONE, dst=ONE) into a UNORM buffer,
	// these gamma values add in gamma space and saturate at 1.0 -- the faithful
	// PSX-style additive that Forward+'s pre-tonemap linear blend cannot do.
	frag_color = params.color;
}
