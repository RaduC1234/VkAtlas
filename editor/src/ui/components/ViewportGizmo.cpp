#include "ViewportGizmo.hpp"

#include "core/IconRegistry.hpp"
#include "ui/panels/ViewportPanel.hpp"

#include <ImViewGuizmo.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Atlas::Editor {
    // ── Math helpers ──────────────────────────────────────────────────────

    glm::vec3 ViewportGizmo::meshLocalCenter(const Mesh &mesh) {
        const auto &verts = mesh.vertices();
        if (verts.empty()) return {};

        glm::vec3 lo = verts.front().position;
        glm::vec3 hi = verts.front().position;
        for (const auto &v: verts) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
        return (lo + hi) * 0.5f;
    }

    glm::vec3 ViewportGizmo::safeDirection(const glm::vec3 &direction, const glm::vec3 &fallback) {
        const float lengthSquared = glm::dot(direction, direction);
        return lengthSquared > 1e-8f ? direction * glm::inversesqrt(lengthSquared) : fallback;
    }

    glm::quat ViewportGizmo::rotationYXZ(const glm::vec3 &r) {
        return glm::angleAxis(r.y, glm::vec3{0, 1, 0})
               * glm::angleAxis(r.x, glm::vec3{1, 0, 0})
               * glm::angleAxis(r.z, glm::vec3{0, 0, 1});
    }

    glm::vec3 ViewportGizmo::eulerFromRotationYXZ(const glm::quat &q) {
        const glm::mat3 m = glm::mat3_cast(q);
        const float x = std::asin(glm::clamp(-m[2][1], -1.0f, 1.0f));
        const float cosX = std::cos(x);
        float y = 0.0f, z = 0.0f;
        if (std::abs(cosX) > 0.0001f) {
            y = std::atan2(m[2][0], m[2][2]);
            z = std::atan2(m[0][1], m[1][1]);
        } else {
            y = std::atan2(-m[0][2], m[0][0]);
        }
        return {x, glm::mod(y, glm::two_pi<float>()), z};
    }

    glm::vec3 ViewportGizmo::lightDirectionFromTransform(const TransformComponent &transform) {
        const glm::vec3 forward{
            std::cos(transform.rotation.x) * std::sin(transform.rotation.y),
            -std::sin(transform.rotation.x),
            std::cos(transform.rotation.x) * std::cos(transform.rotation.y)
        };
        return safeDirection(-forward);
    }

    glm::vec3 ViewportGizmo::lightRightFromTransform(const TransformComponent &transform) {
        return safeDirection(rotationYXZ(transform.rotation) * glm::vec3{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    }

    glm::vec3 ViewportGizmo::lightUpFromTransform(const TransformComponent &transform) {
        return safeDirection(rotationYXZ(transform.rotation) * glm::vec3{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    }

    glm::vec3 ViewportGizmo::transformRotationFromLightDirection(const glm::vec3 &direction) {
        const glm::vec3 normalized = safeDirection(direction);
        const glm::vec3 forward = -normalized;
        return {
            std::asin(glm::clamp(-forward.y, -1.0f, 1.0f)),
            glm::mod(std::atan2(forward.x, forward.z), glm::two_pi<float>()),
            0.0f
        };
    }

    // ── ImViewGuizmo bridge ───────────────────────────────────────────────

    ImViewGuizmo::TransformOperation ViewportGizmo::toImViewOperation(const ObjectGizmoMode mode) {
        switch (mode) {
            case ObjectGizmoMode::Translate: return ImViewGuizmo::TRANSFORM_TRANSLATE;
            case ObjectGizmoMode::Rotate: return ImViewGuizmo::TRANSFORM_ROTATE;
            case ObjectGizmoMode::Scale: return ImViewGuizmo::TRANSFORM_SCALE;
        }
        return ImViewGuizmo::TRANSFORM_TRANSLATE;
    }

    const char *ViewportGizmo::lightTypeName(const LightType type) {
        switch (type) {
            case LightType::POINT: return "Point Light";
            case LightType::SPOT: return "Spot Light";
            case LightType::DIRECTIONAL: return "Directional Light";
            case LightType::RECT: return "Rectangle Light";
            case LightType::UNKNOWN: break;
        }
        return "Light";
    }

    const char *ViewportGizmo::lightIconName(const LightType type) {
        switch (type) {
            case LightType::POINT: return "light_point";
            case LightType::SPOT: return "light_spot";
            case LightType::DIRECTIONAL: return "light_directional";
            case LightType::RECT: return "light_rectangle";
            case LightType::UNKNOWN: break;
        }
        return "light_point";
    }

    // ── Projection ────────────────────────────────────────────────────────

    bool ViewportGizmo::projectPoint(
        const Camera::Data &cameraData,
        const glm::vec3 &worldPosition,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        ImVec2 &outScreen) {
        return projectWorldToViewport(cameraData.viewProjection, worldPosition, imageMin, imageSize, outScreen);
    }

    bool ViewportGizmo::projectWorldToViewport(
        const glm::mat4 &vp,
        const glm::vec3 &world,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        ImVec2 &out) {
        const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0001f) return false;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1 || ndc.x > 1 || ndc.y < -1 || ndc.y > 1 || ndc.z < 0 || ndc.z > 1)
            return false;

        out = {
            imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
            imageMin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y
        };
        return true;
    }

    bool ViewportGizmo::projectCorner(
        const glm::mat4 &vp,
        const glm::vec3 &world,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        ImVec2 &out) {
        const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0001f) return false;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out = {
            imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
            imageMin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y
        };
        return true;
    }

    bool ViewportGizmo::projectLightBillboard(
        const Camera::Data &cam,
        const glm::vec3 &world,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        const float worldSize,
        std::array<ImVec2, 4> &corners,
        ImVec2 &center) {
        const glm::mat4 invView = glm::inverse(cam.view);
        const glm::vec3 right = glm::normalize(glm::vec3(invView * glm::vec4(1, 0, 0, 0)));
        const glm::vec3 up = glm::normalize(glm::vec3(invView * glm::vec4(0, 1, 0, 0)));
        const float half = worldSize * 0.5f;

        const std::array<glm::vec3, 4> wc = {
            world + (-right + up) * half,
            world + (right + up) * half,
            world + (right - up) * half,
            world + (-right - up) * half,
        };

        for (int i = 0; i < 4; ++i)
            if (!projectCorner(cam.viewProjection, wc[i], imageMin, imageSize, corners[i]))
                return false;

        return projectWorldToViewport(cam.viewProjection, world, imageMin, imageSize, center);
    }

    bool ViewportGizmo::projectRectLight(
        const Camera::Data &cam,
        const glm::vec3 &position,
        const LightComponent &light,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        std::array<ImVec2, 4> &corners,
        ImVec2 &center,
        ImVec2 &widthHandle,
        ImVec2 &heightHandle) {
        const glm::vec3 right = glm::normalize(light.rectRight);
        const glm::vec3 up = glm::normalize(light.rectUp);
        const float halfWidth = std::max(light.width, 0.01f) * 0.5f;
        const float halfHeight = std::max(light.height, 0.01f) * 0.5f;

        const std::array<glm::vec3, 4> worldCorners = {
            position - right * halfWidth + up * halfHeight,
            position + right * halfWidth + up * halfHeight,
            position + right * halfWidth - up * halfHeight,
            position - right * halfWidth - up * halfHeight,
        };

        for (int i = 0; i < 4; ++i) {
            if (!projectCorner(cam.viewProjection, worldCorners[i], imageMin, imageSize, corners[i])) {
                return false;
            }
        }

        return projectWorldToViewport(cam.viewProjection, position, imageMin, imageSize, center)
               && projectCorner(cam.viewProjection, position + right * (halfWidth + 0.18f), imageMin, imageSize, widthHandle)
               && projectCorner(cam.viewProjection, position + up * (halfHeight + 0.18f), imageMin, imageSize, heightHandle);
    }

    // ── Billboard drawing ─────────────────────────────────────────────────

    ImU32 ViewportGizmo::lightIconColor(const LightComponent &light) {
        const glm::vec3 c = glm::clamp(light.color, glm::vec3{0}, glm::vec3{1});
        return IM_COL32(
            static_cast<int>(c.r * 255),
            static_cast<int>(c.g * 255),
            static_cast<int>(c.b * 255),
            255);
    }

    void ViewportGizmo::drawLightBillboard(
        ImDrawList &dl,
        const std::array<ImVec2, 4> &corners,
        const ImVec2 center,
        const LightComponent &light,
        const bool selected,
        IconRegistry &icons) {
        const float w = std::abs(corners[1].x - corners[0].x) + std::abs(corners[1].y - corners[0].y);
        const float h = std::abs(corners[3].x - corners[0].x) + std::abs(corners[3].y - corners[0].y);
        const float sz = std::max(w, h);

        // Shadow
        dl.AddRectFilled(
            {center.x - sz * 0.5f + 1.5f, center.y - sz * 0.5f + 1.5f},
            {center.x + sz * 0.5f + 1.5f, center.y + sz * 0.5f + 1.5f},
            IM_COL32(0, 0, 0, 60), sz * 0.15f);

        // Selection ring
        if (selected) {
            dl.AddCircle(center, sz * 0.54f, IM_COL32(255, 255, 255, 200), 32, 1.5f);
            dl.AddCircle(center, sz * 0.54f, IM_COL32(88, 140, 230, 120), 32, 3.5f);
        }

        // Icon
        const ImU32 tint = lightIconColor(light);
        const uint32_t pixels = static_cast<uint32_t>(std::max(16.0f, sz));
        const auto &ic = icons.get(lightIconName(light.type), pixels);

        if (ic.valid()) {
            dl.AddImage(
                ic.textureId(),
                {center.x - sz * 0.5f, center.y - sz * 0.5f},
                {center.x + sz * 0.5f, center.y + sz * 0.5f},
                {0, 0}, {1, 1}, tint);
        } else {
            // Fallback circle
            dl.AddCircleFilled(center, sz * 0.38f, (tint & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 180));
            dl.AddCircle(center, sz * 0.38f, tint, 24, selected ? 2.0f : 1.2f);
        }
    }

    void ViewportGizmo::drawGenericBillboard(
        ImDrawList &dl,
        const std::array<ImVec2, 4> &corners,
        const ImVec2 center,
        const ImU32 tint,
        const char *iconName,
        const bool selected,
        IconRegistry &icons) {
        const float w = std::abs(corners[1].x - corners[0].x) + std::abs(corners[1].y - corners[0].y);
        const float h = std::abs(corners[3].x - corners[0].x) + std::abs(corners[3].y - corners[0].y);
        const float sz = std::max(w, h);

        dl.AddRectFilled(
            {center.x - sz * 0.5f + 1.5f, center.y - sz * 0.5f + 1.5f},
            {center.x + sz * 0.5f + 1.5f, center.y + sz * 0.5f + 1.5f},
            IM_COL32(0, 0, 0, 60), sz * 0.15f);

        if (selected) {
            dl.AddCircle(center, sz * 0.54f, IM_COL32(255, 255, 255, 200), 32, 1.5f);
            dl.AddCircle(center, sz * 0.54f, IM_COL32(88, 140, 230, 120), 32, 3.5f);
        }

        const uint32_t pixels = static_cast<uint32_t>(std::max(16.0f, sz));
        const auto &ic = icons.get(iconName, pixels);

        if (ic.valid()) {
            dl.AddImage(
                ic.textureId(),
                {center.x - sz * 0.5f, center.y - sz * 0.5f},
                {center.x + sz * 0.5f, center.y + sz * 0.5f},
                {0, 0}, {1, 1}, tint);
        } else {
            dl.AddCircleFilled(center, sz * 0.38f, (tint & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 180));
            dl.AddCircle(center, sz * 0.38f, tint, 24, selected ? 2.0f : 1.2f);
        }
    }

    void ViewportGizmo::drawRectLight(
        ImDrawList &dl,
        const std::array<ImVec2, 4> &corners,
        const ImVec2 center,
        const ImVec2 widthHandle,
        const ImVec2 heightHandle,
        const LightComponent &light,
        const bool hoveredWidth,
        const bool hoveredHeight) {
        const ImU32 fill = IM_COL32(255, 220, 128, 32);
        const ImU32 border = IM_COL32(255, 220, 128, 230);
        const ImU32 axisWidth = hoveredWidth ? IM_COL32(90, 180, 255, 255) : IM_COL32(90, 180, 255, 210);
        const ImU32 axisHeight = hoveredHeight ? IM_COL32(120, 245, 145, 255) : IM_COL32(120, 245, 145, 210);

        dl.AddQuadFilled(corners[0], corners[1], corners[2], corners[3], fill);
        dl.AddQuad(corners[0], corners[1], corners[2], corners[3], border, 2.0f);
        dl.AddLine(center, widthHandle, axisWidth, hoveredWidth ? 3.0f : 2.0f);
        dl.AddLine(center, heightHandle, axisHeight, hoveredHeight ? 3.0f : 2.0f);
        dl.AddCircleFilled(widthHandle, hoveredWidth ? 6.5f : 5.0f, axisWidth);
        dl.AddCircleFilled(heightHandle, hoveredHeight ? 6.5f : 5.0f, axisHeight);

        const glm::vec3 c = glm::clamp(light.color, glm::vec3{0}, glm::vec3{1});
        dl.AddCircleFilled(center, 4.0f, IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), 230));
    }
} // namespace Atlas::Editor
