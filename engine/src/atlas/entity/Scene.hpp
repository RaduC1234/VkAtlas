#pragma once

#include <entt/entt.hpp>

namespace Atlas {
    class Scene {
    public:
        virtual ~Scene() = default;

        virtual void onLoad();
        virtual void onUpdate();
        virtual void onRender();
        virtual void onDelete();

        static Scene loadSceneFromJson(const std::string &json);
        static std::string saveSceneToJson(const Scene &scene);
    protected:
        entt::registry registry;
    };
}
