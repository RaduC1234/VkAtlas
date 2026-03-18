#include "Camera.hpp"

#include <cassert>
#include <limits>
#include <glm/gtc/quaternion.hpp>


namespace Atlas {
    void Camera::setOrthographicProjection(float left, float right, float top, float bottom, float near, float far) {
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

    Camera::Data Camera::getData(bool xr, XrView eyes[]) const {
        Data data{};

        if (xr) {
            data.projection[0] = data.projection[1] = this->projectionMatrix;
            data.view[0] = data.view[1] = this->viewMatrix;
            data.viewProjection[0] = data.viewProjection[1] = this->projectionMatrix * this->viewMatrix;
        } else {
            const XrView *e = eyes ? eyes : defaultViews;

            for (int i = 0; i < 2; ++i) {
                data.projection[i] = projectionFromFov(e[i].fov, nearPlane);
                data.view[i] = viewFromPose(e[i].pose, viewMatrix);
                data.viewProjection[i] = data.projection[i] * data.view[i];
            }
        }

        // Extract frustum planes from the first eye (or the mono camera). For XR, callers can
        // choose to compute per-eye planes later if needed.
        const glm::mat4 vp = data.viewProjection[0];

        glm::vec4 row0 = glm::vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        glm::vec4 row1 = glm::vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        glm::vec4 row2 = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        glm::vec4 row3 = glm::vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

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
        data.direction = glm::normalize(glm::vec3(invView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

        data.nearPlane = this->nearPlane;
        data.farPlane = this->farPlane;

        return data;
    }

    glm::mat4 Camera::projectionFromFov(const XrFovf fov, float nearZ) {
        const float l = std::tan(fov.angleLeft);
        const float r = std::tan(fov.angleRight);
        const float u = std::tan(fov.angleLeft);
        const float d = std::tan(fov.angleLeft);

        const float w = r - l;
        const float h = u - d;

        glm::mat4 proj{0.0f};
        proj[0][0] = 2.0f / w;
        proj[1][1] = 2.0f / h;
        proj[2][0] = (r + l) / w; // horizontal asymmetry
        proj[2][1] = (u + d) / h; // vertical   asymmetry
        proj[2][2] = 0.0f; // reversed-Z infinite far
        proj[2][3] = -1.0f; // perspective divide
        proj[3][2] = nearZ; // maps near -> 1.0

        return proj;
    }

    glm::mat4 Camera::viewFromPose(const XrPosef &pose, const glm::mat4 &headView) {
        const glm::quat orientation{
            pose.orientation.w,
            pose.orientation.x,
            pose.orientation.y,
            pose.orientation.z
        };
        const glm::vec3 position{
            pose.position.x,
            pose.position.y,
            pose.position.z
        };

        const glm::mat3 rotInv = glm::transpose(glm::mat3_cast(orientation));
        const glm::vec3 posInv = -(rotInv * position);

        glm::mat4 invEye{1.0f};
        invEye[0] = glm::vec4(rotInv[0], 0.0f);
        invEye[1] = glm::vec4(rotInv[1], 0.0f);
        invEye[2] = glm::vec4(rotInv[2], 0.0f);
        invEye[3] = glm::vec4(posInv, 1.0f);

        return invEye * headView;
    }
}
