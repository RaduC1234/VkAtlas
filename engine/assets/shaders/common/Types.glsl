
struct CameraData {
    mat4  projection;
    mat4  view;
    mat4  viewProjection;
    vec4  frustumPlanes[6];
    vec3  position;
    float nearPlane;
    vec3  direction;
    float farPlane;
};

struct DebugData {
    float iblMultiplier;
    float exposureMultiplier;
    uint  viewMode;
};
