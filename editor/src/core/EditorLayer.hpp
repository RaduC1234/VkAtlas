#pragma once

#include <Atlas.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "EditorHistory.hpp"
#include "IconRegistry.hpp"
#include "ImGuiLayer.hpp"

#include "renderer/resources/GPUTexture.hpp"
#include "ui/panels/AssetExplorerPanel.hpp"
#include "ui/panels/HierarchyPanel.hpp"
#include "ui/panels/InspectorPanel.hpp"
#include "ui/panels/ViewportPanel.hpp"
#include "ui/widgets/MaterialEditor.hpp"

namespace Atlas::Editor {
    class EditorLayer final : public Layer {
    public:
        explicit EditorLayer(ProjectLayer &projectLayer);
        void onAttach() override;
        void onDetach() override;
        void onUpdate(float deltaTime) override;
        void onRender(FrameContext frameContext) override;
        void onImGuiRender() override;

    private:
        void handleShortcuts();
        void drawMenuBar();
        void drawStartupScreen();
        void drawLoadingScreen();
        void loadSplashTexture();
        void createNewProject();
        void openProject();
        void loadProject(const std::filesystem::path &manifestPath);
        void processPendingProjectLoad();
        void ensureEditorCamera();
        void saveLevel();
        void importIntoLevel();
        void exportFramebuffer();
        void copySelection();
        void pasteClipboard();
        void openMaterialEditor(entt::entity ownerEntity, AssetHandle<Material> materialHandle);
        AssetHandle<Material> ensureUniqueEditableMaterial(entt::entity ownerEntity, AssetHandle<Material> materialHandle);
        void drawMaterialEditorWindow();
        void flushMaterialEditorEdit();
        void undo();
        void redo();
        static std::string buildProjectFilter();
        static std::string buildImportFilter(const std::vector<std::string> &extensions);
        static std::string buildFramebufferExportFilter();
        static bool hasSupportedExtension(const std::string &path, const std::vector<std::string> &extensions);

        struct EntityClipboardEntry {
            bool hasSceneNode = false;
            SceneNodeComponent sceneNode;
            bool hasTransform = false;
            TransformComponent transform;
            bool hasModel = false;
            ModelComponent model;
            bool hasMaterial = false;
            MaterialComponent material;
            bool hasLight = false;
            LightComponent light;
            bool hasCamera = false;
            CameraComponent camera;
            bool hasSkybox = false;
            SkyboxComponent skybox;
            bool hasPostProcessing = false;
            PostProcessingVolumeComponent postProcessing;
            bool hasScript = false;
            ScriptComponent script;
        };

        ProjectLayer &projectLayer;
        entt::entity selectedEntity = entt::null;
        std::vector<entt::entity> selectedEntities;
        std::vector<EntityClipboardEntry> entityClipboard;
        EditorHistory history;
        bool materialEditorOpen = false;
        entt::entity materialEditorOwner = entt::null;
        AssetHandle<Material> materialEditorHandle;
        MaterialEditState materialEditState;
        enum class ProjectLoadPhase { None, ShowScreen, Execute };
        ProjectLoadPhase projectLoadPhase_ = ProjectLoadPhase::None;
        float loadingAnimTime_ = 0.0f;
        std::filesystem::path pendingProjectManifestPath;

        std::unique_ptr<GPUTexture> splashTexture;
        VkDescriptorSet splashDescriptor = VK_NULL_HANDLE;

        std::unique_ptr<ImGuiLayer> imguiLayer;
        std::unique_ptr<IconRegistry> iconRegistry;
        std::shared_ptr<HierarchyPanel> hierarchyPanel;
        std::shared_ptr<InspectorPanel> inspectorPanel;
        std::shared_ptr<ViewportPanel> viewportPanel;
        std::shared_ptr<AssetExplorerPanel> assetExplorerPanel;
    };
}
