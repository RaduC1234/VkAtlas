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

        static glm::quat rotationYXZ(const glm::vec3 &eulerYXZ);
        static glm::vec3 eulerFromRotationYXZ(const glm::quat &rotation);

        static ImViewGuizmo::TransformOperation toImViewOperation(ObjectGizmoMode mode);

        static bool projectLightBillboard(const Camera::Data &cameraData, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, float worldSize, std::array<ImVec2, 4> &outCorners, ImVec2 &outCenter);
        static const char *lightTypeName(LightType type);
        static void drawLightBillboard(ImDrawList &dl, const std::array<ImVec2, 4> &corners, ImVec2 center, const LightComponent &light, bool selected, IconRegistry &icons);

    private:
        static const char *lightIconName(LightType type);
        static ImU32 lightIconColor(const LightComponent &light);

        static bool projectWorldToViewport(const glm::mat4 &viewProjection, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, ImVec2 &outScreen);
        static bool projectCorner(const glm::mat4 &viewProjection, const glm::vec3 &worldPosition, ImVec2 imageMin, ImVec2 imageSize, ImVec2 &outScreen);
    };
}
