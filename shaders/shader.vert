#version 450

// Inputs from the Vertex Buffer
layout(location = 0) in vec4 inPositionPinned;
layout(location = 1) in vec4 inVelocity;
layout(location = 2) in vec4 inNormal;

// Outputs to the Fragment Shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPos;

// Uniforms
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} push;

void main() {
    vec3 worldPos = inPositionPinned.xyz;
    gl_Position = push.mvp * vec4(worldPos, 1.0);
    
    fragPos = worldPos;
    
    // Pass the smoothed normal to the fragment shader
    fragNormal = normalize(mat3(push.model) * inNormal.xyz);
}