#pragma once

#include "Panel.hpp"

#include <Atlas.hpp>

namespace Atlas::Editor {
    class ViewportPanel final : public Panel {
    public:
        explicit ViewportPanel(ProjectLayer &projectLayer);
        ~ViewportPanel() override;

        void onDetach() override;
        void onImGuiRender() override;

    private:
        void createViewportTexture();
        void destroyViewportTexture();

        ProjectLayer &projectLayer;
        VkImageView viewportImageView = VK_NULL_HANDLE;
        VkImageLayout viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkDescriptorSet viewportTexture = VK_NULL_HANDLE;
    };
}
