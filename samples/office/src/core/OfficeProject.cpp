#include <Atlas.hpp>

#include <filesystem>
#include <string>

class OfficeProject final : public Atlas::IProjectModule {
public:
    Atlas::IScene *createScene(Atlas::ProjectContext &context, const std::string &levelPath) override {
        const std::filesystem::path resolvedLevelPath = std::filesystem::path(levelPath).is_absolute()
            ? std::filesystem::path(levelPath)
            : context.projectRoot / levelPath;

        return new Atlas::LevelScene(
            context.renderer,
            context.assets,
            resolvedLevelPath,
            context.projectRoot,
            context.assetRoot);
    }
};


ATLAS_PROJECT(OfficeProject)
