#pragma once

#include <core/Layer.hpp>

#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <imgui.h>
#include <vulkan/vulkan.h>

namespace Atlas {
    class ProjectLayer;
}

namespace Atlas::Editor {
    class EditorLayer final : public Layer {
    public:
        explicit EditorLayer(ProjectLayer &projectLayer);

        void onAttach() override;
        void onDetach() override;
        void onUpdate(float deltaTime) override;
        void onImGuiRender() override;

    private:
        void createViewportTexture();
        void destroyViewportTexture();
        void drawTitleBar();
        void drawRenderSettings();
        void drawViewport();
        void drawSceneHierarchy();
        void drawEntityNode(entt::registry &registry, entt::entity entity);
        const char *entityName(entt::registry &registry, entt::entity entity) const;

        ProjectLayer &projectLayer;
        float frameTime = 0.0f;
        entt::entity selectedEntity{entt::null};
        VkImageView viewportImageView = VK_NULL_HANDLE;
        VkImageLayout viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkDescriptorSet viewportTexture = VK_NULL_HANDLE;
    };
}
