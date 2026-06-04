#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Atlas {
    class Camera {
    public:
        struct alignas(16) Data {
            glm::mat4 projection;
            glm::mat4 view;
            glm::mat4 viewProjection;
            glm::vec4 frustumPlanes[6];
            glm::vec3 position;
            float nearPlane;
            glm::vec3 direction;
            float farPlane;
        };

        enum class Projection : uint32_t {
            PERSPECTIVE = 0,
            ORTHOGRAPHIC
        };
        
        void setOrthographicProjection(float left, float right, float top, float bottom, float near, float far);
        void setPerspectiveProjection(float fovY, float aspect, float near, float far);

        void setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up = {0.0f, -1.0f, 0.0f});
        void setViewTarget(glm::vec3 position, glm::vec3 direction, glm::vec3 up = {0.0f, -1.0f, 0.0f});
        void setViewYXZ(glm::vec3 position, glm::vec3 rotation);

        const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }
        const glm::mat4& getViewMatrix() const { return viewMatrix; }

        Data getData() const;
        Projection getTypeProjection() const { return projection; }

    private:
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        float nearPlane{0.1f};
        float farPlane{100.0f};
        Projection projection{Projection::PERSPECTIVE};
    };
}
