#include "scenes/OfficeScene.hpp"


class OfficeProject final : public Atlas::IProjectModule {
public:
    void onProjectLoaded(Atlas::ProjectContext & /*context*/) override {
    }

    Atlas::IScene *createScene(Atlas::ProjectContext &context, const std::string &sceneId) override {
        if (sceneId != "OfficeScene" && sceneId != "LookdevScene") {
            AT_WARN("OfficeProjectModule: unknown scene '{}', falling back to OfficeScene", sceneId);
        }

        return new Atlas::OfficeScene(context.renderer, context.assets);
    }

    void destroyScene(Atlas::IScene *scene) override {
        delete scene;
    }

    void onProjectUnloaded(Atlas::ProjectContext & /*context*/) override {
    }
};


ATLAS_PROJECT(OfficeProject)
