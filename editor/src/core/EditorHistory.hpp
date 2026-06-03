#pragma once

#include <Atlas.hpp>

#include <string>
#include <vector>

namespace Atlas::Editor {
    class EditorHistory {
    public:
        bool canUndo() const;
        bool canRedo() const;

        void clear();
        void undo(entt::registry &registry);
        void redo(entt::registry &registry);

        void recordName(entt::entity entity, std::string before, std::string after);
        void recordVisibility(entt::entity entity, bool before, bool after);
        void recordTransform(entt::entity entity, const TransformComponent &before, const TransformComponent &after);
        void recordMaterial(entt::entity entity, const MaterialComponent &before, const MaterialComponent &after);
        void recordMaterialAsset(entt::entity entity, AssetHandle<Material> material, const Material &before, const Material &after);
        void recordLight(entt::entity entity, const LightComponent &before, const LightComponent &after);

    private:
        enum class EntryType {
            Name,
            Visibility,
            Transform,
            Material,
            MaterialAsset,
            Light
        };

        struct Entry {
            EntryType type;
            entt::entity entity = entt::null;
            std::string beforeName;
            std::string afterName;
            bool beforeVisible = true;
            bool afterVisible = true;
            TransformComponent beforeTransform;
            TransformComponent afterTransform;
            MaterialComponent beforeMaterial;
            MaterialComponent afterMaterial;
            AssetHandle<Material> materialHandle;
            Material beforeMaterialAsset;
            Material afterMaterialAsset;
            LightComponent beforeLight;
            LightComponent afterLight;
        };

        void push(Entry entry);
        void apply(entt::registry &registry, const Entry &entry, bool redo);
        static void refreshCamera(entt::registry &registry, entt::entity entity);
        static void refreshLightFromTransform(entt::registry &registry, entt::entity entity);
        static void refreshTransformFromLight(entt::registry &registry, entt::entity entity);

        std::vector<Entry> undoStack;
        std::vector<Entry> redoStack;
    };
}
