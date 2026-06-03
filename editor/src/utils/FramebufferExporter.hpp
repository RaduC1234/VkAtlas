#pragma once

#include <string>

namespace Atlas {
    class Renderer;
}

namespace Atlas::Editor {
    class FramebufferExporter {
    public:
        static bool exportSceneOutput(Renderer &renderer, const std::string &path);
    };
}
