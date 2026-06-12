#include "EditorLayer.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <utility>

#include <imgui.h>

#include "asset/Texture.hpp"
#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "renderer/resources/GPUTexture.hpp"
#include "project/ProjectCreator.hpp"
#include "project/ProjectResourceImporter.hpp"
#include "utils/FileDialogs.hpp"
#include "utils/FramebufferExporter.hpp"

namespace Atlas::Editor {
    bool editorLayerIsInternalEditorMaterialPath(const std::string &path) {
        return path.starts_with("##editor/materials/") || path.starts_with("##editor\\materials\\");
    }

    uint32_t editorLayerMaterialUserCount(entt::registry &registry, const AssetHandle<Material> &materialHandle) {
        uint32_t count = 0;
        for (const entt::entity entity: registry.view<MaterialComponent>()) {
            if (registry.get<MaterialComponent>(entity).materialHandle == materialHandle) {
                ++count;
            }
        }
        return count;
    }

    std::string editorLayerUniqueInternalMaterialPath(AssetManager &assets, const std::string &baseName) {
        const std::string basePath = "##editor/materials/" + baseName;
        if (!assets.find<Material>(basePath).valid()) {
            return basePath;
        }

        for (uint32_t index = 1; index < 100000; ++index) {
            const std::string path = basePath + "." + std::to_string(index);
            if (!assets.find<Material>(path).valid()) {
                return path;
            }
        }

        return basePath + ".99999";
    }

    bool editorLayerEntityCopyable(entt::registry &registry, const entt::entity entity) {
        if (entity == entt::null || !registry.valid(entity) || registry.all_of<TransientComponent>(entity)) {
            return false;
        }

        const auto *node = registry.try_get<SceneNodeComponent>(entity);
        return !node || !node->deleted;
    }

    bool editorLayerEntityNameExists(entt::registry &registry, const std::string &name) {
        for (const entt::entity entity: registry.view<SceneNodeComponent>()) {
            if (registry.all_of<TransientComponent>(entity)) {
                continue;
            }

            const auto &node = registry.get<SceneNodeComponent>(entity);
            if (!node.deleted && node.name == name) {
                return true;
            }
        }

        return false;
    }

    std::string editorLayerUniqueEntityName(entt::registry &registry, const std::string &sourceName) {
        const std::string base = sourceName.empty() ? "Entity Copy" : sourceName + " Copy";
        if (!editorLayerEntityNameExists(registry, base)) {
            return base;
        }

        for (uint32_t index = 1; index < 100000; ++index) {
            const std::string candidate = base + "." + std::to_string(index);
            if (!editorLayerEntityNameExists(registry, candidate)) {
                return candidate;
            }
        }

        return base + ".99999";
    }

    EditorLayer::EditorLayer(ProjectLayer &projectLayer) : Layer("EditorLayer"), projectLayer(projectLayer) {
    }

    void EditorLayer::loadSplashTexture() {
        auto &renderer = projectLayer.getRenderer();
        try {
            auto texture = Texture::fromFile(AssetManager::resolveFilePath("##editor/splash.png").string());
            splashTexture = std::make_unique<GPUTexture>(renderer.device(), *texture);
            VkCommandBuffer cmd = renderer.device().beginGraphicsCommands();
            splashTexture->recordUpload(cmd);
            renderer.device().endGraphicsCommands(cmd);
            splashTexture->onUploadComplete();
            splashDescriptor = ImGuiLayer::addTexture(
                splashTexture->getSampler(),
                splashTexture->getImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } catch (const std::exception &error) {
            AT_WARN("EditorLayer: failed to load splash image: {}", error.what());
        }
    }

    void EditorLayer::onAttach() {
        ATLAS_PROFILE_SCOPE("EditorLayer::onAttach");
        auto &renderer = projectLayer.getRenderer();
        imguiLayer = std::make_unique<ImGuiLayer>(
            renderer.device(),
            renderer.window(),
            renderer.getOverlayRenderPass(Renderer::OverlayLoadOp::Clear),
            static_cast<uint32_t>(renderer.getImageCount()));
        loadSplashTexture();
        iconRegistry = std::make_unique<IconRegistry>(renderer.device());
        hierarchyPanel = std::make_shared<HierarchyPanel>(projectLayer, selectedEntity, selectedEntities, history);
        inspectorPanel = std::make_shared<InspectorPanel>(
            projectLayer,
            selectedEntity,
            history,
            [this](entt::entity ownerEntity, AssetHandle<Material> materialHandle) {
                openMaterialEditor(ownerEntity, materialHandle);
            }
        );
        viewportPanel = std::make_shared<ViewportPanel>(projectLayer, selectedEntity, selectedEntities, history, *iconRegistry);
        assetExplorerPanel = std::make_shared<AssetExplorerPanel>(projectLayer, *iconRegistry);
    }

    void EditorLayer::onDetach() {
        ATLAS_PROFILE_SCOPE("EditorLayer::onDetach");
        if (hierarchyPanel) hierarchyPanel->onDetach();
        if (inspectorPanel) inspectorPanel->onDetach();
        if (viewportPanel) viewportPanel->onDetach();
        if (assetExplorerPanel) assetExplorerPanel->onDetach();
        iconRegistry.reset();
        if (splashDescriptor != VK_NULL_HANDLE) {
            ImGuiLayer::removeTexture(splashDescriptor);
            splashDescriptor = VK_NULL_HANDLE;
        }
        splashTexture.reset();
        imguiLayer.reset();
    }

    void EditorLayer::onUpdate(float deltaTime) {
        ATLAS_PROFILE_SCOPE("EditorLayer::onUpdate");
        if (projectLoadPhase_ != ProjectLoadPhase::None)
            loadingAnimTime_ += deltaTime;
        processPendingProjectLoad();
    }

    void EditorLayer::onRender(FrameContext frameContext) {
        ATLAS_PROFILE_SCOPE("EditorLayer::onRender");
        if (!imguiLayer) {
            return;
        }

        auto &renderer = projectLayer.getRenderer();
        {
            ATLAS_PROFILE_SCOPE("EditorLayer::buildImGui");
            imguiLayer->beginFrame(true);
            onImGuiRender();
            imguiLayer->endFrame();
        }

        {
            ATLAS_PROFILE_SCOPE("EditorLayer::recordOverlay");
            ATLAS_PROFILE_GPU_ZONE(renderer.device().gpuProfilerContext(), frameContext.graphicsCommandBuffer, "Editor::OverlayPass");
            renderer.beginOverlayRenderPass(frameContext.graphicsCommandBuffer, Renderer::OverlayLoadOp::Clear);
            imguiLayer->render(frameContext.graphicsCommandBuffer);
            renderer.endOverlayRenderPass(frameContext.graphicsCommandBuffer);
        }
    }

    void EditorLayer::onImGuiRender() {
        ATLAS_PROFILE_SCOPE("EditorLayer::onImGuiRender");

        if (projectLoadPhase_ != ProjectLoadPhase::None) {
            drawLoadingScreen();
            return;
        }

        if (projectLayer.project().rootPath().empty()) {
            drawStartupScreen();
            return;
        }

        {
            ATLAS_PROFILE_SCOPE("EditorLayer::handleShortcuts");
            handleShortcuts();
        }
        {
            ATLAS_PROFILE_SCOPE("EditorLayer::drawMenuBar");
            drawMenuBar();
        }
        if (viewportPanel) {
            ATLAS_PROFILE_SCOPE("ViewportPanel::onImGuiRender");
            viewportPanel->onImGuiRender();
        }
        if (hierarchyPanel) {
            ATLAS_PROFILE_SCOPE("HierarchyPanel::onImGuiRender");
            hierarchyPanel->onImGuiRender();
        }
        if (inspectorPanel) {
            ATLAS_PROFILE_SCOPE("InspectorPanel::onImGuiRender");
            inspectorPanel->onImGuiRender();
        }
        if (assetExplorerPanel) {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::onImGuiRender");
            assetExplorerPanel->onImGuiRender();
        }
        {
            ATLAS_PROFILE_SCOPE("EditorLayer::drawMaterialEditorWindow");
            drawMaterialEditorWindow();
        }
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
        } else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            copySelection();
        } else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            pasteClipboard();
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
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, selectedEntity != entt::null || !selectedEntities.empty())) {
                copySelection();
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !entityClipboard.empty())) {
                pasteClipboard();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Show All Panels")) {
                if (hierarchyPanel) hierarchyPanel->visible = true;
                if (inspectorPanel) inspectorPanel->visible = true;
                if (viewportPanel) viewportPanel->visible = true;
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

    void EditorLayer::copySelection() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        entityClipboard.clear();

        std::vector<entt::entity> copyList = selectedEntities;
        if (copyList.empty() && selectedEntity != entt::null) {
            copyList.push_back(selectedEntity);
        }

        for (const entt::entity entity: copyList) {
            if (!editorLayerEntityCopyable(registry, entity)) {
                continue;
            }

            EntityClipboardEntry entry{};
            if (const auto *component = registry.try_get<SceneNodeComponent>(entity)) {
                entry.hasSceneNode = true;
                entry.sceneNode = *component;
                entry.sceneNode.parent = entt::null;
                entry.sceneNode.children.clear();
                entry.sceneNode.deleted = false;
            }
            if (const auto *component = registry.try_get<TransformComponent>(entity)) {
                entry.hasTransform = true;
                entry.transform = *component;
            }
            if (const auto *component = registry.try_get<ModelComponent>(entity)) {
                entry.hasModel = true;
                entry.model = *component;
            }
            if (const auto *component = registry.try_get<MaterialComponent>(entity)) {
                entry.hasMaterial = true;
                entry.material = *component;
            }
            if (const auto *component = registry.try_get<LightComponent>(entity)) {
                entry.hasLight = true;
                entry.light = *component;
            }
            if (const auto *component = registry.try_get<CameraComponent>(entity)) {
                entry.hasCamera = true;
                entry.camera = *component;
            }
            if (const auto *component = registry.try_get<SkyboxComponent>(entity)) {
                entry.hasSkybox = true;
                entry.skybox = *component;
            }
            if (const auto *component = registry.try_get<PostProcessingVolumeComponent>(entity)) {
                entry.hasPostProcessing = true;
                entry.postProcessing = *component;
            }
            if (const auto *component = registry.try_get<ScriptComponent>(entity)) {
                entry.hasScript = true;
                entry.script = *component;
            }

            entityClipboard.push_back(std::move(entry));
        }
    }

    void EditorLayer::pasteClipboard() {
        auto *scene = projectLayer.project().scene();
        if (!scene || entityClipboard.empty()) {
            return;
        }

        auto &registry = scene->getRegistry();
        selectedEntities.clear();

        for (const EntityClipboardEntry &entry: entityClipboard) {
            const entt::entity entity = registry.create();

            if (entry.hasSceneNode) {
                SceneNodeComponent node = entry.sceneNode;
                node.name = editorLayerUniqueEntityName(registry, node.name);
                node.parent = entt::null;
                node.children.clear();
                node.deleted = false;
                registry.emplace<SceneNodeComponent>(entity, std::move(node));
            }

            if (entry.hasTransform) {
                TransformComponent transform = entry.transform;
                transform.translation += glm::vec3{0.35f, 0.0f, 0.35f};
                registry.emplace<TransformComponent>(entity, transform);
                registry.patch<TransformComponent>(entity);
            }
            if (entry.hasModel) {
                registry.emplace<ModelComponent>(entity, entry.model);
            }
            if (entry.hasMaterial) {
                registry.emplace<MaterialComponent>(entity, entry.material);
                registry.patch<MaterialComponent>(entity);
            }
            if (entry.hasLight) {
                registry.emplace<LightComponent>(entity, entry.light);
                registry.patch<LightComponent>(entity);
            }
            if (entry.hasCamera) {
                CameraComponent camera = entry.camera;
                if (entry.hasTransform) {
                    camera.camera.setViewYXZ(registry.get<TransformComponent>(entity).translation, registry.get<TransformComponent>(entity).rotation);
                }
                registry.emplace<CameraComponent>(entity, camera);
                registry.patch<CameraComponent>(entity);
            }
            if (entry.hasSkybox) {
                registry.emplace<SkyboxComponent>(entity, entry.skybox);
            }
            if (entry.hasPostProcessing) {
                registry.emplace<PostProcessingVolumeComponent>(entity, entry.postProcessing);
                registry.patch<PostProcessingVolumeComponent>(entity);
            }
            if (entry.hasScript) {
                registry.emplace<ScriptComponent>(entity, entry.script);
            }

            selectedEntities.push_back(entity);
            selectedEntity = entity;
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
        projectLoadPhase_ = ProjectLoadPhase::ShowScreen;
        projectLayer.getRenderer().window().setDecorated(false);
    }

    void EditorLayer::processPendingProjectLoad() {
        if (projectLoadPhase_ == ProjectLoadPhase::ShowScreen) {
            projectLoadPhase_ = ProjectLoadPhase::Execute;
            return;
        }

        if (projectLoadPhase_ != ProjectLoadPhase::Execute) {
            return;
        }

        projectLoadPhase_ = ProjectLoadPhase::None;
        loadingAnimTime_ = 0.0f;
        projectLayer.getRenderer().window().setDecorated(true);

        const std::filesystem::path manifestPath = pendingProjectManifestPath;
        pendingProjectManifestPath.clear();

        flushMaterialEditorEdit();
        if (viewportPanel) {
            viewportPanel->onDetach();
        }
        if (assetExplorerPanel) {
            assetExplorerPanel->onDetach();
        }
        projectLayer.getRenderer().clearSceneOutputImage();
        selectedEntity = entt::null;
        selectedEntities.clear();
        entityClipboard.clear();
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
        materialHandle = ensureUniqueEditableMaterial(ownerEntity, materialHandle);
        materialEditorOpen = true;
        materialEditorOwner = ownerEntity;
        materialEditorHandle = materialHandle;
        materialEditState = {};
    }

    AssetHandle<Material> EditorLayer::ensureUniqueEditableMaterial(
        const entt::entity ownerEntity,
        AssetHandle<Material> materialHandle) {
        if (!materialHandle.valid() || !materialHandle.get() || !materialHandle.hasPath()) {
            return materialHandle;
        }

        if (!editorLayerIsInternalEditorMaterialPath(materialHandle.path())) {
            return materialHandle;
        }

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return materialHandle;
        }

        auto &registry = scene->getRegistry();
        if (ownerEntity == entt::null || !registry.valid(ownerEntity)) {
            return materialHandle;
        }

        auto *component = registry.try_get<MaterialComponent>(ownerEntity);
        if (!component || component->materialHandle != materialHandle) {
            return materialHandle;
        }

        if (editorLayerMaterialUserCount(registry, materialHandle) <= 1) {
            return materialHandle;
        }

        MaterialComponent before = *component;
        auto material = std::make_shared<Material>(*materialHandle.get());

        std::string materialName = material->name;
        if (const auto *node = registry.try_get<SceneNodeComponent>(ownerEntity); node && !node->name.empty()) {
            materialName = node->name + " Material";
        } else if (materialName.empty()) {
            materialName = "Material";
        }
        material->name = materialName;

        const std::string materialPath = editorLayerUniqueInternalMaterialPath(projectLayer.assetManager(), materialName);
        AssetHandle<Material> uniqueHandle = projectLayer.assetManager().store<Material>(std::move(material), materialPath);

        registry.patch<MaterialComponent>(ownerEntity, [&](auto &materialComponent) {
            materialComponent.materialHandle = uniqueHandle;
        });
        history.recordMaterial(ownerEntity, before, registry.get<MaterialComponent>(ownerEntity));

        return uniqueHandle;
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

    void EditorLayer::drawLoadingScreen() {
        const ImGuiIO &io = ImGui::GetIO();
        const ImVec2 displaySize = io.DisplaySize;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySize);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##loading_screen", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoNav);

        if (splashDescriptor != VK_NULL_HANDLE) {
            ImGui::Image((ImTextureID)splashDescriptor, displaySize);
        }

        // "Loading..." overlay centered on screen
        const char *loadingText = "Loading...";
        const ImVec2 textSize = ImGui::CalcTextSize(loadingText);
        const ImVec2 textPos{
            (displaySize.x - textSize.x) * 0.5f,
            (displaySize.y - textSize.y) * 0.5f
        };
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddText(textPos, IM_COL32(255, 255, 255, 220), loadingText);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::drawStartupScreen() {
        const ImGuiIO &io = ImGui::GetIO();
        const ImVec2 displaySize = io.DisplaySize;

        // Full-screen background
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySize);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##startup_bg", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs);
        ImGui::End();
        ImGui::PopStyleVar();

        // Centered card
        constexpr float cardW = 340.0f;
        constexpr float cardH = 210.0f;
        const ImVec2 cardPos{(displaySize.x - cardW) * 0.5f, (displaySize.y - cardH) * 0.5f};

        ImGui::SetNextWindowPos(cardPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(cardW, cardH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
        ImGui::Begin("##startup_card", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings);

        // Title
        const char *title = "Atlas Editor";
        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        ImGui::SetCursorPosX((cardW - titleSize.x) * 0.5f);
        ImGui::TextUnformatted(title);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();

        // Buttons
        constexpr float btnW = cardW - 56.0f; // card width minus 2*padding
        constexpr float btnH = 36.0f;

        if (ImGui::Button("New Project", ImVec2(btnW, btnH))) {
            createNewProject();
        }

        ImGui::Spacing();

        if (ImGui::Button("Open Project", ImVec2(btnW, btnH))) {
            openProject();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}
