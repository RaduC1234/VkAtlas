#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <openxr/openxr.h>

namespace Atlas {
    class Camera {
    public:
        struct alignas(16) Data {
            glm::mat4 projection[2];
            glm::mat4 view[2];
            glm::mat4 viewProjection[2];
            glm::vec4 frustumPlanes[6];
            glm::vec3 position;
            float nearPlane;
            glm::vec3 direction;
            float farPlane;
        };

        void setOrthographicProjection(float left, float right, float top, float bottom, float near, float far);
        void setPerspectiveProjection(float fovY, float aspect, float near, float far);

        void setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up = {0.0f, -1.0f, 0.0f});
        void setViewTarget(glm::vec3 position, glm::vec3 direction, glm::vec3 up = {0.0f, -1.0f, 0.0f});
        void setViewYXZ(glm::vec3 position, glm::vec3 rotation);

        const glm::mat4 &getProjection() const { return projectionMatrix; }
        const glm::mat4 &getView() const { return viewMatrix; }

        Data getData(bool xr = true, XrView eyes[] = nullptr) const;

    private:
        static glm::mat4 projectionFromFov(const XrFovf fov, float nearZ);
        static glm::mat4 viewFromPose(const XrPosef& pose, const glm::mat4& headView);

        static constexpr float kHalfIPD = 0.0315f; // 63mm / 2
        static constexpr float kHalfFov = 0.7854f; // 45 deg -> 90 deg total
        static constexpr XrView defaultViews[2] = {
            {XR_TYPE_VIEW, nullptr, {{0.0f, 0.0f, 0.0f, 1.0f}, {-kHalfIPD, 0.0f, 0.0f}}, {-kHalfFov, kHalfFov, kHalfFov, -kHalfFov}},
            {XR_TYPE_VIEW, nullptr, {{0.0f, 0.0f, 0.0f, 1.0f}, {kHalfIPD, 0.0f, 0.0f}}, {-kHalfFov, kHalfFov, kHalfFov, -kHalfFov}}
        };

        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        float nearPlane{0.1f};
        float farPlane{100.0f};
    };
}
