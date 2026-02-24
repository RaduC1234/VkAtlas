#version 460

// Push
layout(push_constant) uniform Push {
    vec3 position;  // Billboard world position
    float scale;    // Billboard size
    vec4 color;
} push;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionMatrix;
    mat4 viewMatrix;
} ubo;

layout(location = 0) out vec2 fragUV;

// Quad positions for billboard (2 triangles)
const vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0),  // Bottom-left
    vec2( 1.0, -1.0),  // Bottom-right
    vec2( 1.0,  1.0),  // Top-right
    vec2(-1.0, -1.0),  // Bottom-left
    vec2( 1.0,  1.0),  // Top-right
    vec2(-1.0,  1.0)   // Top-left
);

void main() {
    vec2 quadPos = positions[gl_VertexIndex];

    // Pass UV coordinates to fragment shader
    fragUV = quadPos * 0.5 + 0.5;

    // Extract camera right and up vectors from the view matrix
    // The view matrix transforms from world to view space
    // So the inverse would give us camera orientation in world space
    vec3 cameraRight = vec3(ubo.viewMatrix[0][0], ubo.viewMatrix[1][0], ubo.viewMatrix[2][0]);
    vec3 cameraUp = vec3(ubo.viewMatrix[0][1], ubo.viewMatrix[1][1], ubo.viewMatrix[2][1]);

    // Build billboard position in world space
    // Start with the billboard center, then offset by quad position scaled by camera axes
    vec3 worldPos = push.position
                  + cameraRight * quadPos.x * push.scale
                  + cameraUp * quadPos.y * push.scale;

    // Transform to clip space
    gl_Position = ubo.projectionMatrix * ubo.viewMatrix * vec4(worldPos, 1.0);
}