
struct GPUObjectData {
    mat4  modelMatrix;
    mat4  normalMatrix;
    uvec4 textureIndices;
    vec4  baseColor;
    vec4  materialFactors;
};

struct Light {
    uint  type;
    float intensity;
    float range;
    float innerConeAngle;
    vec3  color;
    float outerConeAngle;
    vec3  position;
    float width;
    vec3  direction;
    float height;
};
