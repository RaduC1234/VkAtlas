#include "Keyboard.hpp"
#include "IInputProvider.hpp"
#include <cassert>

namespace Atlas {
    std::vector<bool> Keyboard::keyPressed(349, false);
    std::queue<uint32_t> Keyboard::keyTyped;
    bool Keyboard::nativeInput{false};
    IInputProvider *Keyboard::provider = nullptr;

    void Keyboard::setProvider(IInputProvider *p) { provider = p; }

    bool Keyboard::isKeyPressed(KeyCode code) {
        assert(provider != nullptr && "Call Keyboard::setProvider() before querying input");
        return provider->isKeyPressed(code);
    }
}
