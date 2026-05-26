#pragma once

#include "asset/AssetManager.hpp"
#include "core/Core.hpp"
#include "renderer/Camera.hpp"

namespace Atlas {
    struct SceneNodeComponent {
        std::string name;
        entt::entity parent = entt::null;
        std::vector<entt::entity> children;
        bool visible = true;
        bool deleted = false;
    };

    struct TransformComponent {
        glm::vec3 translation{};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
        glm::vec3 rotation{};

        // Matrix corrsponds to Translate * Ry * Rx * Rz * Scale
        // Rotations correspond to Tait-bryan angles of Y(1), X(2), Z(3)
        // https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
        glm::mat4 mat4() const {
            const float c3 = glm::cos(rotation.z);
            const float s3 = glm::sin(rotation.z);
            const float c2 = glm::cos(rotation.x);
            const float s2 = glm::sin(rotation.x);
            const float c1 = glm::cos(rotation.y);
            const float s1 = glm::sin(rotation.y);
            return glm::mat4{
                {
                    scale.x * (c1 * c3 + s1 * s2 * s3),
                    scale.x * (c2 * s3),
                    scale.x * (c1 * s2 * s3 - c3 * s1),
                    0.0f,
                },
                {
                    scale.y * (c3 * s1 * s2 - c1 * s3),
                    scale.y * (c2 * c3),
                    scale.y * (c1 * c3 * s2 + s1 * s3),
                    0.0f,
                },
                {
                    scale.z * (c2 * s1),
                    scale.z * (-s2),
                    scale.z * (c1 * c2),
                    0.0f,
                },
                {translation.x, translation.y, translation.z, 1.0f}
            };
        }

        glm::mat3 normalMatrix() const {
            const float c3 = glm::cos(rotation.z);
            const float s3 = glm::sin(rotation.z);
            const float c2 = glm::cos(rotation.x);
            const float s2 = glm::sin(rotation.x);
            const float c1 = glm::cos(rotation.y);
            const float s1 = glm::sin(rotation.y);
            const glm::vec3 invScale = 1.0f / scale;

            return glm::mat3{
                {
                    invScale.x * (c1 * c3 + s1 * s2 * s3),
                    invScale.x * (c2 * s3),
                    invScale.x * (c1 * s2 * s3 - c3 * s1)
                },
                {
                    invScale.y * (c3 * s1 * s2 - c1 * s3),
                    invScale.y * (c2 * c3),
                    invScale.y * (c1 * c3 * s2 + s1 * s3)
                },
                {
                    invScale.z * (c2 * s1),
                    invScale.z * (-s2),
                    invScale.z * (c1 * c2)
                }
            };
        }
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TransformComponent, translation, scale, rotation);

    struct ModelComponent {
        AssetHandle<Mesh> meshHandle;
    };

    struct CameraComponent {
        Camera camera{};
    };

    enum class ShadingModel : uint32_t {
        STANDARD_PBR = 0,
        CLOTH = 1
    };

    struct MaterialComponent {
        ShadingModel shadingModel{ShadingModel::STANDARD_PBR};

        glm::vec4 baseColor = glm::vec4{1.0f};
        AssetHandle<Texture> albedoTexture;
        AssetHandle<Texture> normalMap;
        AssetHandle<Texture> metallicRoughnessMap;
        AssetHandle<Texture> ambientOcclusion;
        bool alphaMasked{false};
        bool transparent{false};

        float sheenStrength{0.0f};
    };

    struct SkyboxComponent {
        AssetHandle<Cubemap> skyboxHandle;
        AssetHandle<Cubemap> irradianceHandle;
        AssetHandle<Cubemap> prefilterHandle;
    };

    enum class LightType : uint32_t {
        UNKNOWN = 0,
        POINT, // Uses KHR_punctual_lights
        SPOT, // Uses KHR_punctual_lights
        DIRECTIONAL, // Uses KHR_punctual_lights
        RECT // Uses ATLAS_special_lights
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(LightType, {
                                 {LightType::UNKNOWN, "Unknown"},
                                 {LightType::POINT, "Point"},
                                 {LightType::SPOT, "Spot"},
                                 {LightType::DIRECTIONAL, "Directional"},
                                 {LightType::RECT, "Rectangle"},
                                 })

    struct LightComponent {
        LightType type{LightType::POINT};
        glm::vec3 color{1.0f};
        float intensity{1.0f};
        float range{0.0f}; // 0.0 = infinite

        // For SPOT lights
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        float innerConeAngle{0.0f};
        float outerConeAngle{glm::radians(45.0f)};

        // For RECT lights
        float width{0.0f};
        float height{0.0f};
        glm::vec3 rectRight{1.0f, 0.0f, 0.0f};
        glm::vec3 rectUp{0.0f, 1.0f, 0.0f};
    };

    struct PostProcessingVolumeComponent {
        float exposure{1.0f};
        float contrast{1.0f};
        float saturation{1.0};
        glm::vec3 colorTint = {1.0f, 1.0f, 1.0f};
    };
}
