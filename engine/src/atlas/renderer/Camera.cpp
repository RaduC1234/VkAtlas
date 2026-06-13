#include "Camera.hpp"

#include <cassert>
#include <limits>


namespace Atlas {
    void Camera::setOrthographicProjection(float left, float right, float top, float bottom, float near, float far) {
        nearPlane = near;
        farPlane = far;
        projectionMatrix = glm::mat4{1.0f};
        projectionMatrix[0][0] = 2.f / (right - left);
        projectionMatrix[1][1] = 2.f / (bottom - top);
        projectionMatrix[2][2] = 1.f / (far - near);
        projectionMatrix[3][0] = -(right + left) / (right - left);
        projectionMatrix[3][1] = -(bottom + top) / (bottom - top);
        projectionMatrix[3][2] = -near / (far - near);
    }

    void Camera::setPerspectiveProjection(float fovY, float aspect, float near, float far) {
        assert(glm::abs(aspect - std::numeric_limits<float>::epsilon()) > 0.0f);
        nearPlane = near;
        farPlane = far;
        const float tanHalfFovy = tan(fovY / 2.f);
        projectionMatrix = glm::mat4{0.0f};
        projectionMatrix[0][0] = 1.f / (aspect * tanHalfFovy);
        projectionMatrix[1][1] = 1.f / (tanHalfFovy);
        projectionMatrix[2][2] = far / (far - near);
        projectionMatrix[2][3] = 1.f;
        projectionMatrix[3][2] = -(far * near) / (far - near);
    }

    void Camera::setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up) {
        const glm::vec3 w{glm::normalize(direction)};
        const glm::vec3 u{glm::normalize(glm::cross(w, up))};
        const glm::vec3 v{glm::cross(w, u)};

        viewMatrix = glm::mat4{1.f};
        viewMatrix[0][0] = u.x;
        viewMatrix[1][0] = u.y;
        viewMatrix[2][0] = u.z;
        viewMatrix[0][1] = v.x;
        viewMatrix[1][1] = v.y;
        viewMatrix[2][1] = v.z;
        viewMatrix[0][2] = w.x;
        viewMatrix[1][2] = w.y;
        viewMatrix[2][2] = w.z;
        viewMatrix[3][0] = -glm::dot(u, position);
        viewMatrix[3][1] = -glm::dot(v, position);
        viewMatrix[3][2] = -glm::dot(w, position);
    }

    void Camera::setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up) {
        setViewDirection(position, target - position, up);
    }

    void Camera::setViewYXZ(glm::vec3 position, glm::vec3 rotation) {
        const float c3 = glm::cos(rotation.z);
        const float s3 = glm::sin(rotation.z);
        const float c2 = glm::cos(rotation.x);
        const float s2 = glm::sin(rotation.x);
        const float c1 = glm::cos(rotation.y);
        const float s1 = glm::sin(rotation.y);
        const glm::vec3 u{(c1 * c3 + s1 * s2 * s3), (c2 * s3), (c1 * s2 * s3 - c3 * s1)};
        const glm::vec3 v{(c3 * s1 * s2 - c1 * s3), (c2 * c3), (c1 * c3 * s2 + s1 * s3)};
        const glm::vec3 w{(c2 * s1), (-s2), (c1 * c2)};
        viewMatrix = glm::mat4{1.f};
        viewMatrix[0][0] = u.x;
        viewMatrix[1][0] = u.y;
        viewMatrix[2][0] = u.z;
        viewMatrix[0][1] = v.x;
        viewMatrix[1][1] = v.y;
        viewMatrix[2][1] = v.z;
        viewMatrix[0][2] = w.x;
        viewMatrix[1][2] = w.y;
        viewMatrix[2][2] = w.z;
        viewMatrix[3][0] = -glm::dot(u, position);
        viewMatrix[3][1] = -glm::dot(v, position);
        viewMatrix[3][2] = -glm::dot(w, position);
    }

    Camera::Data Camera::getData() const {
        Data data{};
        data.projection = this->projectionMatrix;
        data.view = this->viewMatrix;
        data.viewProjection = this->projectionMatrix * this->viewMatrix;

        glm::vec4 row0 = glm::vec4(data.viewProjection[0][0], data.viewProjection[1][0], data.viewProjection[2][0], data.viewProjection[3][0]);
        glm::vec4 row1 = glm::vec4(data.viewProjection[0][1], data.viewProjection[1][1], data.viewProjection[2][1], data.viewProjection[3][1]);
        glm::vec4 row2 = glm::vec4(data.viewProjection[0][2], data.viewProjection[1][2], data.viewProjection[2][2], data.viewProjection[3][2]);
        glm::vec4 row3 = glm::vec4(data.viewProjection[0][3], data.viewProjection[1][3], data.viewProjection[2][3], data.viewProjection[3][3]);

        glm::vec4 planes[6];
        planes[0] = row3 + row0; // left
        planes[1] = row3 - row0; // right
        planes[2] = row3 + row1; // bottom
        planes[3] = row3 - row1; // top
        planes[4] = row3 + row2; // near
        planes[5] = row3 - row2; // far

        for (int i = 0; i < 6; ++i) {
            glm::vec3 n = glm::vec3(planes[i]);
            float length = glm::length(n);
            if (length > 0.0f) planes[i] /= length;
            data.frustumPlanes[i] = planes[i];
        }

        // Camera position and direction in world space
        glm::mat4 invView = glm::inverse(viewMatrix);
        data.position = glm::vec3(invView[3]); // translation column
        data.direction = glm::normalize(glm::vec3(invView * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f))); // view space is +Z forward

        data.nearPlane = this->nearPlane;
        data.farPlane = this->farPlane;

        return data;

    }
}
