#include "EditorLayer.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <utility>

#include <imgui.h>

#include "core/Log.hpp"
#include "project/ProjectCreator.hpp"
#include "project/ProjectResourceImporter.hpp"
#include "utils/FileDialogs.hpp"
#include "utils/FramebufferExporter.hpp"

namespace Atlas::Editor {
    EditorLayer::EditorLayer(ProjectLayer &projectLayer) : Layer("EditorLayer"), projectLayer(projectLayer) {
    }

    void EditorLayer::onAttach() {
        auto &renderer = projectLayer.getRenderer();
        imguiLayer = std::make_unique<ImGuiLayer>(
            renderer.device(),
            renderer.window(),
            renderer.getOverlayRenderPass(Renderer::OverlayLoadOp::Clear),
            static_cast<uint32_t>(renderer.getImageCount()));
        iconRegistry = std::make_unique<IconRegistry>(renderer.device());
        hierarchyPanel = std::make_shared<HierarchyPanel>(projectLayer, selectedEntity, history);
        inspectorPanel = std::make_shared<InspectorPanel>(
            projectLayer,
            selectedEntity,
            history,
            [this](entt::entity ownerEntity, AssetHandle<Material> materialHandle) {
                openMaterialEditor(ownerEntity, materialHandle);
            }
        );
        viewportPanel = std::make_shared<ViewportPanel>(projectLayer, selectedEntity, history, *iconRegistry);
        renderSettingsPanel = std::make_shared<RenderSettingsPanel>(projectLayer);
        assetExplorerPanel = std::make_shared<AssetExplorerPanel>(projectLayer);
    }

    void EditorLayer::onDetach() {
        if (hierarchyPanel) hierarchyPanel->onDetach();
        if (inspectorPanel) inspectorPanel->onDetach();
        if (viewportPanel) viewportPanel->onDetach();
        if (renderSettingsPanel) renderSettingsPanel->onDetach();
        if (assetExplorerPanel) assetExplorerPanel->onDetach();
        iconRegistry.reset();
        imguiLayer.reset();
    }

    void EditorLayer::onUpdate(float) {
        processPendingProjectLoad();
        ensureEditorCamera();
    }

    void EditorLayer::onRender(FrameContext frameContext) {
        if (!imguiLayer) {
            return;
        }

        auto &renderer = projectLayer.getRenderer();
        imguiLayer->beginFrame(true);
        onImGuiRender();
        imguiLayer->endFrame();

        renderer.beginOverlayRenderPass(frameContext.graphicsCommandBuffer, Renderer::OverlayLoadOp::Clear);
        imguiLayer->render(frameContext.graphicsCommandBuffer);
        renderer.endOverlayRenderPass(frameContext.graphicsCommandBuffer);
    }

    void EditorLayer::onImGuiRender() {
        handleShortcuts();
        drawMenuBar();
        if (renderSettingsPanel) renderSettingsPanel->onImGuiRender();
        if (viewportPanel) viewportPanel->onImGuiRender();
        if (hierarchyPanel) hierarchyPanel->onImGuiRender();
        if (inspectorPanel) inspectorPanel->onImGuiRender();
        if (assetExplorerPanel) assetExplorerPanel->onImGuiRender();
        drawMaterialEditorWindow();
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
            if (ImGui::MenuItem("Open Project...")) {
                openProject();
            }

            if (ImGui::MenuItem("Save Level")) {
                saveLevel();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Import into Level...")) {
                importIntoLevel();
            }

            if (ImGui::MenuItem("Export Framebuffer...")) {
                exportFramebuffer();
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
                if (assetExplorerPanel) assetExplorerPanel->visible = true;
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
            if (ImGui::MenuItem("Asset Explorer", nullptr, assetExplorerPanel && assetExplorerPanel->visible)) {
                if (assetExplorerPanel) assetExplorerPanel->visible = true;
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

    void EditorLayer::saveLevel() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            AT_WARN("EditorLayer: cannot save level, no level is loaded");
            return;
        }

        const ProjectManifest &manifest = projectLayer.project().manifest();
        if (manifest.startupLevel.empty()) {
            AT_WARN("EditorLayer: cannot save level, project manifest has no startupLevel");
            return;
        }

        const std::filesystem::path levelPath = std::filesystem::path(manifest.startupLevel).is_absolute()
            ? std::filesystem::path(manifest.startupLevel)
            : projectLayer.project().rootPath() / manifest.startupLevel;

        if (levelPath.extension() != ".atlaslevel") {
            AT_WARN("EditorLayer: startup target '{}' is not a serialized Atlas level", manifest.startupLevel);
            return;
        }

        try {
            LevelSerializer::save(levelPath, scene->getRegistry());
            AT_INFO("EditorLayer: saved level '{}'", levelPath.string());
        } catch (const std::exception &error) {
            AT_ERROR("EditorLayer: failed to save level '{}': {}", levelPath.string(), error.what());
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
            loadProject(result.manifestPath);
            AT_INFO("EditorLayer: created project '{}' at '{}'", result.name, result.rootPath.string());
        } catch (const std::exception &error) {
            AT_ERROR("EditorLayer: failed to create project: {}", error.what());
        }
    }

    void EditorLayer::openProject() {
        const std::string filter = buildProjectFilter();
        const std::string manifestPath = FileDialogs::openFile(filter.c_str());

        if (manifestPath.empty()) {
            return;
        }

        loadProject(manifestPath);
    }

    void EditorLayer::loadProject(const std::filesystem::path &manifestPath) {
        pendingProjectManifestPath = manifestPath;
        pendingProjectLoad = true;
    }

    void EditorLayer::processPendingProjectLoad() {
        if (!pendingProjectLoad) {
            return;
        }

        const std::filesystem::path manifestPath = pendingProjectManifestPath;
        pendingProjectManifestPath.clear();
        pendingProjectLoad = false;

        flushMaterialEditorEdit();
        selectedEntity = entt::null;
        history.clear();
        materialEditorOpen = false;
        materialEditorOwner = entt::null;
        materialEditorHandle = {};
        materialEditState = {};

        try {
            projectLayer.loadProject(manifestPath);
            AT_INFO("EditorLayer: opened project '{}'", manifestPath.string());
        } catch (const std::exception &error) {
            AT_ERROR("EditorLayer: failed to open project '{}': {}", manifestPath.string(), error.what());
        }
    }

    void EditorLayer::ensureEditorCamera() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        for (const entt::entity entity: registry.view<EditorCameraComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && node->deleted) {
                continue;
            }

            return;
        }

        const entt::entity cameraEntity = registry.create();

        SceneNodeComponent node{};
        node.name = "Editor Camera";
        registry.emplace<SceneNodeComponent>(cameraEntity, std::move(node));

        TransformComponent transform{};
        transform.translation = {0.0f, 0.0f, 3.0f};
        registry.emplace<TransformComponent>(cameraEntity, transform);

        registry.emplace<CameraComponent>(cameraEntity);
        registry.emplace<EditorCameraComponent>(cameraEntity);
        registry.emplace<TransientComponent>(cameraEntity);
    }

    void EditorLayer::importIntoLevel() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            AT_WARN("EditorLayer: cannot import asset, no scene is loaded");
            return;
        }

        const auto extensions = ProjectResourceImporter::supportedExtensions();
        const std::string filter = buildImportFilter(extensions);
        const std::string path = FileDialogs::openFile(filter.c_str());

        if (path.empty()) {
            return;
        }

        if (!hasSupportedExtension(path, extensions)) {
            AT_WARN("EditorLayer: unsupported import extension '{}'", std::filesystem::path(path).extension().string());
            return;
        }

        try {
            auto &registry = scene->getRegistry();
            const std::vector<entt::entity> importedEntities = ProjectResourceImporter::importIntoProject(projectLayer, path, registry);
            AT_INFO("EditorLayer: imported {} entities from '{}'", importedEntities.size(), path);
        } catch (const std::exception &error) {
            AT_ERROR("EditorLayer: failed to import '{}': {}", path, error.what());
        }
    }

    void EditorLayer::exportFramebuffer() {
        const std::string filter = buildFramebufferExportFilter();
        const std::string path = FileDialogs::saveFile(filter.c_str());

        if (path.empty()) {
            return;
        }

        try {
            FramebufferExporter::exportSceneOutput(projectLayer.getRenderer(), path);
        } catch (const std::exception &error) {
            AT_ERROR("EditorLayer: failed to export framebuffer: {}", error.what());
        }
    }

    void EditorLayer::openMaterialEditor(entt::entity ownerEntity, AssetHandle<Material> materialHandle) {
        if (!materialHandle.valid()) {
            return;
        }

        flushMaterialEditorEdit();
        materialEditorOpen = true;
        materialEditorOwner = ownerEntity;
        materialEditorHandle = materialHandle;
        materialEditState = {};
    }

    void EditorLayer::drawMaterialEditorWindow() {
        if (!materialEditorOpen) {
            return;
        }

        if (!materialEditorHandle.valid()) {
            flushMaterialEditorEdit();
            materialEditorOpen = false;
            materialEditorOwner = entt::null;
            materialEditorHandle = {};
            materialEditState = {};
            return;
        }

        auto *scene = projectLayer.project().scene();
        entt::registry *registry = scene ? &scene->getRegistry() : nullptr;

        std::string title = "Material Editor";
        if (Material *material = materialEditorHandle.get()) {
            if (!material->name.empty()) {
                title = "Material Editor - " + material->name;
            }
        }
        title += "###Material Editor";

        ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &materialEditorOpen)) {
            MaterialEditor::drawProperties(
                projectLayer,
                history,
                registry,
                materialEditorOwner,
                materialEditorHandle,
                materialEditState
            );
        }
        ImGui::End();

        if (!materialEditorOpen) {
            flushMaterialEditorEdit();
            materialEditorOwner = entt::null;
            materialEditorHandle = {};
            materialEditState = {};
        }
    }

    void EditorLayer::flushMaterialEditorEdit() {
        if (!materialEditState.active || !materialEditState.handle.valid()) {
            return;
        }

        if (Material *material = materialEditState.handle.get()) {
            history.recordMaterialAsset(
                materialEditorOwner,
                materialEditState.handle,
                materialEditState.before,
                *material
            );

            auto *scene = projectLayer.project().scene();
            if (scene && scene->getRegistry().valid(materialEditorOwner)) {
                if (const auto *component = scene->getRegistry().try_get<MaterialComponent>(materialEditorOwner);
                    component && component->materialHandle.hasPath()) {
                    const std::filesystem::path materialHandlePath(component->materialHandle.path());
                    if (!component->materialHandle.path().starts_with("##")) {
                        const std::filesystem::path materialPath = materialHandlePath.is_absolute()
                            ? materialHandlePath
                            : projectLayer.project().assetsPath() / materialHandlePath;
                        try {
                            Material::saveFile(*material, materialPath.string());
                        } catch (const std::exception &error) {
                            AT_ERROR("EditorLayer: failed to save material '{}': {}", materialPath.string(), error.what());
                        }
                    }
                }
            }
        }

        materialEditState = {};
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

    std::string EditorLayer::buildFramebufferExportFilter() {
        std::string filter = "PNG Image (*.png)";
        filter.push_back('\0');
        filter += "*.png";
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
