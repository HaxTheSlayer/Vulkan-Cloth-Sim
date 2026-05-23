#version 450

layout(location = 0) in vec4 position_pinned;
layout(location = 1) in vec4 velocity;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = push.mvp * vec4(position_pinned.xyz, 1.0);

    // Pinned particles = red, free = white
    float pinned = position_pinned.w;
    fragColor = mix(vec3(1.0, 1.0, 1.0), vec3(1.0, 0.1, 0.1), pinned);
}