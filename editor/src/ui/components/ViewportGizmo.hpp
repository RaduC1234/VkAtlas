#pragma once

#include <Atlas.hpp>
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include <array>

#include "ImViewGuizmo.h"

namespace Atlas::Editor {
    class IconRegistry;
    enum class ObjectGizmoMode;

    class ViewportGizmo {
    public:
        ViewportGizmo() = delete;

        static glm::vec3 meshLocalCenter(const Mesh &mesh);

        static glm::vec3 safeDirection(const glm::vec3 &direction, const glm::vec3 &fallback = {0.0f, -1.0f, 0.0f});
        static glm::quat rotationYXZ(const glm::vec3 &eulerYXZ);
        static glm::vec3 eulerFromRotationYXZ(const glm::quat &rotation);
        static glm::vec3 lightDirectionFromTransform(const TransformComponent &transform);
        static glm::vec3 lightRightFromTransform(const TransformComponent &transform);
        static glm::vec3 lightUpFromTransform(const TransformComponent &transform);
        static glm::vec3 transformRotationFromLightDirection(const glm::vec3 &direction);

        static ImViewGuizmo::TransformOperation toImViewOperation(ObjectGizmoMode mode);

        static bool projectPoint(const Camera::Data &cameraData, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, ImVec2 &outScreen);
        static bool projectLightBillboard(const Camera::Data &cameraData, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, float worldSize, std::array<ImVec2, 4> &outCorners, ImVec2 &outCenter);
        static bool projectRectLight(const Camera::Data &cameraData, const glm::vec3 &position, const LightComponent &light, ImVec2 imageMin, ImVec2 imageSize, std::array<ImVec2, 4> &outCorners, ImVec2 &outCenter, ImVec2 &outWidthHandle, ImVec2 &outHeightHandle);
        static const char *lightTypeName(LightType type);
        static void drawLightBillboard(ImDrawList &dl, const std::array<ImVec2, 4> &corners, ImVec2 center, const LightComponent &light, bool selected, IconRegistry &icons);
        static void drawGenericBillboard(ImDrawList &dl, const std::array<ImVec2, 4> &corners, ImVec2 center, ImU32 tint, const char *iconName, bool selected, IconRegistry &icons);
        static void drawRectLight(ImDrawList &dl, const std::array<ImVec2, 4> &corners, ImVec2 center, ImVec2 widthHandle, ImVec2 heightHandle, const LightComponent &light, bool hoveredWidth, bool hoveredHeight);

    private:
        static const char *lightIconName(LightType type);
        static ImU32 lightIconColor(const LightComponent &light);

        static bool projectWorldToViewport(const glm::mat4 &viewProjection, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, ImVec2 &outScreen);
        static bool projectCorner(const glm::mat4 &viewProjection, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, ImVec2 &outScreen);
    };
}
