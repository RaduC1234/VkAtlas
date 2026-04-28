#include "IScene.hpp"
#include <utility>

namespace Atlas {

    IScene::IScene(Renderer &renderer) : renderer(renderer) {}

    void IScene::onLoad(entt::registry &&registry) {
        this->registry = std::move(registry);
    }

    void IScene::onUpdate(float /*deltaTime*/) {}

    void IScene::onRender(FrameContext /*frameContext*/) {}

}

