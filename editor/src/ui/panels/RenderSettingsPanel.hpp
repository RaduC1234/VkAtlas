#pragma once

#include "Panel.hpp"

#include <Atlas.hpp>

namespace Atlas::Editor {
    class RenderSettingsPanel final : public Panel {
    public:
        explicit RenderSettingsPanel(ProjectLayer &projectLayer);

        void onImGuiRender() override;

    private:
        ProjectLayer &projectLayer;
    };
}
