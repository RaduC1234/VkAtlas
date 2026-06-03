#pragma once

#include <entt/entity/registry.hpp>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "system/RenderSystemV2.hpp"

namespace Atlas {
    class IScene {
    public:
        explicit IScene(Renderer &renderer);
        virtual ~IScene() = default;

        virtual void onLoad(entt::registry &&registry);
        virtual void onUpdate(float deltaTime);
        virtual void onRender(FrameContext frameContext);
        virtual void onDelete() { registry.clear(); }

        entt::registry &getRegistry() { return registry; }
        const entt::registry &getRegistry() const { return registry; }
        DebugData &debugData() { return renderDebugData; }
        const DebugData &debugData() const { return renderDebugData; }

    protected:
        entt::registry registry;
        Renderer &renderer;
        DebugData renderDebugData{};
    };
}
