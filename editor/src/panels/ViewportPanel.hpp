#pragma once

#include "core/EditorHistory.hpp"
#include "Panel.hpp"

#include <Atlas.hpp>
#include <glm/gtc/quaternion.hpp>

struct ImVec2;

namespace Atlas::Editor {
    enum class ObjectGizmoMode {
        Translate,
        Rotate,
        Scale
    };

    enum class ObjectGizmoSpace {
        Local,
        World
    };

    class ViewportPanel final : public Panel {
    public:
        ViewportPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity, EditorHistory &history);
        ~ViewportPanel() override;

        void onDetach() override;
        void onImGuiRender() override;

    private:
        void renderToolbar();
        void renderObjectGizmo(ImVec2 imageMin, ImVec2 imageSize, bool viewportHovered);
        void renderViewGizmo(ImVec2 imageMin, ImVec2 imageSize);
        void createViewportTexture();
        void destroyViewportTexture();

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
        EditorHistory &history;
        VkImageView viewportImageView = VK_NULL_HANDLE;
        VkImageLayout viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkDescriptorSet viewportTexture = VK_NULL_HANDLE;
        ObjectGizmoMode objectGizmoMode = ObjectGizmoMode::Translate;
        ObjectGizmoSpace objectGizmoSpace = ObjectGizmoSpace::Local;
        bool objectTransformEditActive = false;
        entt::entity objectTransformEditEntity = entt::null;
        TransformComponent objectTransformEditBefore;
        glm::quat cameraRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
}
