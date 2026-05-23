#include "ViewportPanel.hpp"

#include <cstdint>

#include <imgui.h>

namespace Atlas::Editor {
    ViewportPanel::ViewportPanel(ProjectLayer &projectLayer) : projectLayer(projectLayer) {
    }

    ViewportPanel::~ViewportPanel() {
        destroyViewportTexture();
    }

    void ViewportPanel::onDetach() {
        destroyViewportTexture();
    }

    void ViewportPanel::onImGuiRender() {
        if (!visible) {
            return;
        }

        createViewportTexture();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", &visible);
        ImGui::PopStyleVar();

        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 1.0f && size.y > 1.0f) {
            projectLayer.getRenderer().setSceneViewportExtent({
                static_cast<uint32_t>(size.x),
                static_cast<uint32_t>(size.y)
            });
        }

        if (viewportTexture != VK_NULL_HANDLE && size.x > 1.0f && size.y > 1.0f) {
            ImGui::Image((ImTextureID) viewportTexture, size);
        }

        ImGui::End();
    }

    void ViewportPanel::createViewportTexture() {
        const auto &outputImage = projectLayer.getRenderer().getSceneOutputImage();
        if (!outputImage.valid()) {
            destroyViewportTexture();
            return;
        }

        if (viewportTexture != VK_NULL_HANDLE &&
            viewportImageView == outputImage.imageView &&
            viewportImageLayout == outputImage.imageLayout) {
            return;
        }

        destroyViewportTexture();
        viewportImageView = outputImage.imageView;
        viewportImageLayout = outputImage.imageLayout;
        viewportTexture = ImGuiLayer::addTexture(outputImage.imageView, outputImage.imageLayout);
    }

    void ViewportPanel::destroyViewportTexture() {
        if (viewportTexture != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(projectLayer.getRenderer().device().device());
            ImGuiLayer::removeTexture(viewportTexture);
            viewportTexture = VK_NULL_HANDLE;
        }

        viewportImageView = VK_NULL_HANDLE;
        viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
}
