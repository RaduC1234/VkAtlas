
struct RayPayload {
    vec3  radiance;
    vec3  throughput;
    vec3  origin;
    vec3  direction;
    bool  done;
    uint  seed;
};

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
};

struct ObjectData {
    mat4  modelMatrix;
    mat4  normalMatrix;
    uvec4 textureIndices;
    vec4  baseColor;
    vec4  materialFactors;
    vec4  sheenColorStrength;
    uint  firstIndex;
    uint  indexCount;
    uint  firstVertex;
    uint  flags;
};

struct Light {
    uint  type;
    float intensity;
    float range;
    float innerConeAngle;
    vec4  color;
    float outerConeAngle;
    vec3  position;
    float width;
    vec3  direction;
    float height;
    vec3  rectRight;
    vec3  rectUp;
};
