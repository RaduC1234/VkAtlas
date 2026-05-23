#include "EditorLayer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <imgui.h>

#include "core/Log.hpp"
#include "utils/FileDialogs.hpp"

namespace Atlas::Editor {
    EditorLayer::EditorLayer(ProjectLayer &projectLayer) : Layer("EditorLayer"), projectLayer(projectLayer) {
    }

    void EditorLayer::onAttach() {
        hierarchyPanel = std::make_shared<HierarchyPanel>(projectLayer, selectedEntity);
        inspectorPanel = std::make_shared<InspectorPanel>(projectLayer, selectedEntity);
        viewportPanel = std::make_shared<ViewportPanel>(projectLayer);
        renderSettingsPanel = std::make_shared<RenderSettingsPanel>(projectLayer);
    }

    void EditorLayer::onDetach() {
        if (hierarchyPanel) hierarchyPanel->onDetach();
        if (inspectorPanel) inspectorPanel->onDetach();
        if (viewportPanel) viewportPanel->onDetach();
        if (renderSettingsPanel) renderSettingsPanel->onDetach();
    }

    void EditorLayer::onUpdate(float) {
    }

    void EditorLayer::onImGuiRender() {
        drawMenuBar();
        if (renderSettingsPanel) renderSettingsPanel->onImGuiRender();
        if (viewportPanel) viewportPanel->onImGuiRender();
        if (hierarchyPanel) hierarchyPanel->onImGuiRender();
        if (inspectorPanel) inspectorPanel->onImGuiRender();
    }

    void EditorLayer::drawMenuBar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import into Level...")) {
                importIntoLevel();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Show All Panels")) {
                if (hierarchyPanel) hierarchyPanel->visible = true;
                if (inspectorPanel) inspectorPanel->visible = true;
                if (viewportPanel) viewportPanel->visible = true;
                if (renderSettingsPanel) renderSettingsPanel->visible = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Viewport", nullptr, viewportPanel && viewportPanel->visible)) {
                if (viewportPanel) viewportPanel->visible = true;
            }
            if (ImGui::MenuItem("Scene Hierarchy", nullptr, hierarchyPanel && hierarchyPanel->visible)) {
                if (hierarchyPanel) hierarchyPanel->visible = true;
            }
            if (ImGui::MenuItem("Properties", nullptr, inspectorPanel && inspectorPanel->visible)) {
                if (inspectorPanel) inspectorPanel->visible = true;
            }
            if (ImGui::MenuItem("Render Settings", nullptr, renderSettingsPanel && renderSettingsPanel->visible)) {
                if (renderSettingsPanel) renderSettingsPanel->visible = true;
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void EditorLayer::importIntoLevel() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            AT_WARN("EditorLayer: cannot import asset, no scene is loaded");
            return;
        }

        const auto extensions = projectLayer.assetManager().importerExtensions();
        const std::string filter = buildImportFilter(extensions);
        const std::string path = FileDialogs::openFile(filter.c_str());

        if (path.empty()) {
            return;
        }

        if (!hasSupportedExtension(path, extensions)) {
            AT_WARN("EditorLayer: unsupported import extension '{}'", std::filesystem::path(path).extension().string());
            return;
        }

        projectLayer.assetManager().importAsync(path, scene->getRegistry());
    }

    std::string EditorLayer::buildImportFilter(const std::vector<std::string> &extensions) {
        std::string patterns;
        for (const auto &extension: extensions) {
            if (!patterns.empty()) {
                patterns += ";";
            }
            patterns += "*";
            patterns += extension;
        }

        if (patterns.empty()) {
            patterns = "*.*";
        }

        std::string filter = "Importable Assets (";
        filter += patterns;
        filter += ")";
        filter.push_back('\0');
        filter += patterns;
        filter.push_back('\0');
        filter += "All Files (*.*)";
        filter.push_back('\0');
        filter += "*.*";
        filter.push_back('\0');
        filter.push_back('\0');
        return filter;
    }

    bool EditorLayer::hasSupportedExtension(const std::string &path, const std::vector<std::string> &extensions) {
        std::string extension = std::filesystem::path(path).extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        return std::ranges::find(extensions, extension) != extensions.end();
    }
}
