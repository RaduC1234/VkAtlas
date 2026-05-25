#include "EditorLayer.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>

#include <imgui.h>

#include "core/Log.hpp"
#include "project/ProjectCreator.hpp"
#include "utils/FileDialogs.hpp"

namespace Atlas::Editor {
    EditorLayer::EditorLayer(ProjectLayer &projectLayer) : Layer("EditorLayer"), projectLayer(projectLayer) {
    }

    void EditorLayer::onAttach() {
        hierarchyPanel = std::make_shared<HierarchyPanel>(projectLayer, selectedEntity, history);
        inspectorPanel = std::make_shared<InspectorPanel>(projectLayer, selectedEntity, history);
        viewportPanel = std::make_shared<ViewportPanel>(projectLayer, selectedEntity, history);
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
        handleShortcuts();
        drawMenuBar();
        if (renderSettingsPanel) renderSettingsPanel->onImGuiRender();
        if (viewportPanel) viewportPanel->onImGuiRender();
        if (hierarchyPanel) hierarchyPanel->onImGuiRender();
        if (inspectorPanel) inspectorPanel->onImGuiRender();
    }

    void EditorLayer::handleShortcuts() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        const ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsAnyItemActive()) {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (hierarchyPanel) {
                hierarchyPanel->deleteSelected();
            }
            return;
        }

        if (!io.KeyCtrl) {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                redo();
            } else {
                undo();
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            redo();
        }
    }

    void EditorLayer::drawMenuBar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                createNewProject();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Import into Level...")) {
                importIntoLevel();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history.canUndo())) {
                undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, history.canRedo())) {
                redo();
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

    void EditorLayer::undo() {
        if (auto *scene = projectLayer.project().scene()) {
            history.undo(scene->getRegistry());
        }
    }

    void EditorLayer::redo() {
        if (auto *scene = projectLayer.project().scene()) {
            history.redo(scene->getRegistry());
        }
    }

    void EditorLayer::createNewProject() {
        const std::string filter = buildProjectFilter();
        const std::string manifestPath = FileDialogs::saveFile(filter.c_str());

        if (manifestPath.empty()) {
            return;
        }

        try {
            ProjectCreateInfo info{};
            info.manifestPath = manifestPath;
            info.name = ProjectCreator::defaultNameForPath(info.manifestPath);

            const ProjectCreateResult result = ProjectCreator::create(info);
            AT_INFO(
                "EditorLayer: created project '{}' at '{}'. Build '{}' before loading the project.",
                result.name,
                result.rootPath.string(),
                result.targetName
            );
        } catch (const std::exception &error) {
            AT_ERROR("EditorLayer: failed to create project: {}", error.what());
        }
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

    std::string EditorLayer::buildProjectFilter() {
        std::string filter = "Atlas Project Manifest (*.atlas.json)";
        filter.push_back('\0');
        filter += "*.atlas.json";
        filter.push_back('\0');
        filter += "JSON Files (*.json)";
        filter.push_back('\0');
        filter += "*.json";
        filter.push_back('\0');
        filter += "All Files (*.*)";
        filter.push_back('\0');
        filter += "*.*";
        filter.push_back('\0');
        filter.push_back('\0');
        return filter;
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
