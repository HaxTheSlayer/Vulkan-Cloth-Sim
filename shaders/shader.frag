#version 450

// Inputs from Vertex Shader
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragPos;

// Final output to the screen
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.8)); // Light coming from top-right
    vec3 lightColor = vec3(1.0, 0.95, 0.9);         // Warm sunlight
    vec3 ambientColor = vec3(0.1, 0.15, 0.2);       // Cool sky ambient light
    vec3 clothColor = vec3(0.8, 0.2, 0.2);          // Crimson red cloth

    vec3 normal = normalize(fragNormal);
    
    // If the camera is looking at the back of the triangle, flip the normal so it still catches light properly.
    if (!gl_FrontFacing) {
        normal = -normal;
    }

    // Ambient Lighting
    vec3 ambient = ambientColor * clothColor;

    // Diffuse Lighting
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * clothColor;

    // Final composition
    vec3 finalColor = ambient + diffuse;
    
    outColor = vec4(finalColor, 1.0);
}