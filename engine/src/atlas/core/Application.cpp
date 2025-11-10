#include "Application.hpp"

// std
#include <chrono>

#include "asset/AssetManager.hpp"
#include "system/RenderSystem.hpp"
#include "system/CameraSystem.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <imgui.h>
#include <glm/glm.hpp>

#include "entity/Object.hpp"
#include "renderer/ImGuiLayer.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &spec) : specification(spec) {
        AssetManager::create(this->device, spec.pNativeApp);
        this->window->setWindowIcon("assets/icons/android_robot.png");
        this->window->setTheme(true);

        loadGameObjects();
    }

    Application::~Application() {
        // Destroy AssetManager before Device to free GPU resources.
        AssetManager::destroy();
    }

    void Application::run() {
        ImGuiLayer imGui{device, *window, renderer.getSwapChainRenderPass(), static_cast<uint32_t>(renderer.getImageCount())};
        CameraSystem cameraSystem{*window};
        RenderSystem renderSystem{device, renderer.getSwapChainRenderPass()};

        Camera camera{};
        auto currentTime = std::chrono::high_resolution_clock::now();

        auto cameraObj = registry.create();
        registry.emplace<TransformComponent>(cameraObj);
        registry.emplace<CameraComponent>(cameraObj, camera);

        while (!window->shouldClose()) {
            window->pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration_cast<std::chrono::duration<float>>(newTime - currentTime).count();
            currentTime = newTime;

            float aspect = renderer.getAspectRatio();
            //camera.setOrthographicProjection(-aspect, aspect, -1, 1, -1, 1);
            camera.setPerspectiveProjection(glm::radians(50.0f), aspect, 0.1f, 100.0f);

            if (auto commandBuffer = renderer.beginFrame()) {
                renderSystem.prepareTextures(registry);

                imGui.beginFrame();

                ImGui::Begin("Debug Settings");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::End();

                int frameIndex = renderer.getFrameIndex();

                // Update UBO data
                renderSystem.updateUBO(
                    frameIndex,
                    camera.getProjection(),
                    camera.getView(),
                    glm::vec4(1.0f, 1.0f, 1.0f, 0.005f),  // ambient color
                    glm::vec3(-1.0f),                       // light position
                    glm::vec4(1.0f)                         // light color
                );

                // Render
                renderer.beginSwapChainRenderPass(commandBuffer);
                cameraSystem.update(registry, frameTime);
                renderSystem.render(registry, commandBuffer, frameIndex);

                imGui.endFrame(commandBuffer);

                renderer.endSwapChainRenderPass(commandBuffer);
                renderer.endFrame();
            }
        }

        vkDeviceWaitIdle(device.device());
    }

    void Application::loadGameObjects() {
        auto& assetManager = AssetManager::get();

        // Load assets and get handles
        AssetHandle vase0Mesh = assetManager.loadMesh("models/flat_vase.obj");
        AssetHandle vase1Mesh = assetManager.loadMesh("models/smooth_vase.obj");
        AssetHandle sphereMesh = assetManager.createSphere(0.1f);  // Create procedural sphere instead of loading
        AssetHandle quadMesh = assetManager.loadMesh("models/quad.obj");
        AssetHandle brickTexture = assetManager.loadTexture("textures/Brick_4K_BaseColor.jpg");
        AssetHandle chairTex;

        // Load chair GLB and add it to the scene
        AssetHandle chairMesh = assetManager.loadGltf("models/SM_ArmChair_2.glb");
        if (chairMesh != INVALID_ASSET_HANDLE) {
            auto chairObj = registry.create();
            auto &chairTransform = registry.emplace<TransformComponent>(chairObj);
            chairTransform.translation = glm::vec3{0.0f, 0.0f, 0.0f};
            chairTransform.rotation = glm::vec3{0.0f, glm::radians(-90.0f), 0.0f};
            chairTransform.scale = glm::vec3{1.0f};

            registry.emplace<MaterialComponent>(chairObj);
            auto &chairModel = registry.emplace<ModelComponent>(chairObj);
            chairModel.meshHandle = chairMesh;

            // Attempt to assign the first embedded image as albedo if it exists
            std::string firstImageKey = "models/SM_ArmChair_2.glb#mesh0_prim0_baseColor";
            chairTex = assetManager.loadTexture(firstImageKey);
            if (chairTex != INVALID_ASSET_HANDLE) {
                auto &mat = registry.get<MaterialComponent>(chairObj);
                mat.albedoTexture = chairTex;
            }
        }

        auto gameObject0 = registry.create();
        auto &transform0 = registry.emplace<TransformComponent>(gameObject0);
        transform0.translation = {0.0f, 0.5f, -0.25f};
        transform0.scale = glm::vec3{1.0f};

        registry.emplace<MaterialComponent>(gameObject0);
        auto &model0 = registry.emplace<ModelComponent>(gameObject0);
        model0.meshHandle = vase0Mesh;


        auto gameObject1 = registry.create();
        auto &transform1 = registry.emplace<TransformComponent>(gameObject1);
        transform1.translation = {0.0f, 0.5f, 0.25f};
        transform1.scale = glm::vec3{1.0f};

        registry.emplace<MaterialComponent>(gameObject1);
        auto &model1 = registry.emplace<ModelComponent>(gameObject1);
        model1.meshHandle = vase1Mesh;


        auto gameObject2 = registry.create();
        auto &transform2 = registry.emplace<TransformComponent>(gameObject2);
        transform2.translation = glm::vec3{0.0f};
        transform2.scale = glm::vec3{1.0};

        registry.emplace<MaterialComponent>(gameObject2);
        auto &model2 = registry.emplace<ModelComponent>(gameObject2);
        model2.meshHandle = sphereMesh;


        auto gameObject3 = registry.create();
        auto &transform3 = registry.emplace<TransformComponent>(gameObject3);
        transform3.translation = {0, 0.5f, 0.0f};
        transform3.scale = glm::vec3{2.0f, 1.0f, 2.0f};

        auto &material3 = registry.emplace<MaterialComponent>(gameObject3);
        material3.albedoTexture = brickTexture;

        auto &model3 = registry.emplace<ModelComponent>(gameObject3);
        model3.meshHandle = quadMesh;
    }
} // namespace
