
const float PI                 = 3.14159265359;
const float INV_PI             = 0.31830988618;
const float EPSILON            = 1e-5;
const float MAX_REFLECTION_LOD = 5.0;

const uint LIGHT_TYPE_POINT       = 1u;
const uint LIGHT_TYPE_SPOT        = 2u;
const uint LIGHT_TYPE_DIRECTIONAL = 3u;
const uint LIGHT_TYPE_RECT        = 4u;

const uint MAX_LIGHT_COUNT = 256u;

const uint VIEWMODE_LIT          = 0u;
const uint VIEWMODE_UNLIT        = 1u;
const uint VIEWMODE_CLAY         = 2u;
const uint VIEWMODE_PATH_TRACING = 3u;

const uint MATERIAL_FLAG_ALPHA_MASKED  = 1u << 0u;
const uint MATERIAL_FLAG_CLOTH_CHARLIE = 1u << 1u;
