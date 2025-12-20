// skybox.vert
#version 450

layout(location = 0) out vec3 fragTexCoord;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionMatrix;
    mat4 viewMatrix;
} ubo;

const vec3 positions[36] = vec3[](
// Back face
vec3(-1.0, -1.0, -1.0),
vec3( 1.0,  1.0, -1.0),
vec3( 1.0, -1.0, -1.0),
vec3( 1.0,  1.0, -1.0),
vec3(-1.0, -1.0, -1.0),
vec3(-1.0,  1.0, -1.0),
// Front face
vec3(-1.0, -1.0,  1.0),
vec3( 1.0, -1.0,  1.0),
vec3( 1.0,  1.0,  1.0),
vec3( 1.0,  1.0,  1.0),
vec3(-1.0,  1.0,  1.0),
vec3(-1.0, -1.0,  1.0),
// Left face
vec3(-1.0,  1.0,  1.0),
vec3(-1.0,  1.0, -1.0),
vec3(-1.0, -1.0, -1.0),
vec3(-1.0, -1.0, -1.0),
vec3(-1.0, -1.0,  1.0),
vec3(-1.0,  1.0,  1.0),
// Right face
vec3( 1.0,  1.0,  1.0),
vec3( 1.0, -1.0, -1.0),
vec3( 1.0,  1.0, -1.0),
vec3( 1.0, -1.0, -1.0),
vec3( 1.0,  1.0,  1.0),
vec3( 1.0, -1.0,  1.0),
// Bottom face
vec3(-1.0, -1.0, -1.0),
vec3( 1.0, -1.0, -1.0),
vec3( 1.0, -1.0,  1.0),
vec3( 1.0, -1.0,  1.0),
vec3(-1.0, -1.0,  1.0),
vec3(-1.0, -1.0, -1.0),
// Top face
vec3(-1.0,  1.0, -1.0),
vec3( 1.0,  1.0,  1.0),
vec3( 1.0,  1.0, -1.0),
vec3( 1.0,  1.0,  1.0),
vec3(-1.0,  1.0, -1.0),
vec3(-1.0,  1.0,  1.0)
);

void main() {
    vec3 pos = positions[gl_VertexIndex];
    fragTexCoord = pos;

    mat4 viewRotationOnly = mat4(mat3(ubo.viewMatrix));
    vec4 clipPos = ubo.projectionMatrix * viewRotationOnly * vec4(pos, 1.0);
    gl_Position = clipPos.xyww;
}