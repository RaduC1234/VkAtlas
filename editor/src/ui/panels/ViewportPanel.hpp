#pragma once

#include "core/EditorHistory.hpp"
#include "Panel.hpp"

#include <Atlas.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

struct ImVec2;

namespace Atlas::Editor {
    class IconRegistry;

    enum class ObjectGizmoMode {
        Translate,
        Rotate,
        Scale
    };

    enum class ObjectGizmoSpace {
        Local,
        World
    };

    enum class ViewportPrimitive {
        Cube,
        Square,
        Sphere
    };

    class ViewportPanel final : public Panel {
    public:
        ViewportPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity, EditorHistory &history, IconRegistry &iconRegistry);
        ~ViewportPanel() override;

        void onDetach() override;
        void onImGuiRender() override;

    private:
        void renderToolbar(ImVec2 imageMin, ImVec2 imageSize);
        void renderContextMenu(bool viewportHovered);
        void renderLightBillboards(ImVec2 imageMin, ImVec2 imageSize);
        void renderObjectGizmo(ImVec2 imageMin, ImVec2 imageSize, bool viewportHovered);
        void renderViewGizmo(ImVec2 imageMin, ImVec2 imageSize);
        void addPrimitive(ViewportPrimitive primitive);
        void addLight(LightType type);
        AssetHandle<Mesh> primitiveMesh(ViewportPrimitive primitive);
        AssetHandle<Texture> primitiveWhiteTexture();
        entt::entity activeCamera(entt::registry &registry) const;
        glm::vec3 primitiveSpawnPosition();
        std::string primitiveName(ViewportPrimitive primitive) const;
        std::string lightName(LightType type);
        void createViewportTexture();
        void destroyViewportTexture();

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
        EditorHistory &history;
        IconRegistry &iconRegistry;
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
