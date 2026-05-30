#pragma once

namespace Atlas::Editor {
    class Panel {
    public:
        virtual ~Panel() = default;
        virtual void onDetach() {}
        virtual void onImGuiRender() = 0;
        bool visible = true;
    };
}
