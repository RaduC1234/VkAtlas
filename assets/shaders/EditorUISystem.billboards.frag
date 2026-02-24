#version 460

// Push
layout(push_constant) uniform Push {
    vec3 position;      // Billboard world position
    float scale;        // Billboard size
    vec4 color;
    uint textureIndex;  // Texture to use
} push;

// Input from vertex shader
layout(location = 0) in vec2 fragUV;

// Descriptors
layout(set = 1, binding = 0) uniform sampler2D textures[8]; // 1 - light_point, 2 - light_spot, 3 - light_direction

// Output
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(textures[push.textureIndex], fragUV) * push.color;
}