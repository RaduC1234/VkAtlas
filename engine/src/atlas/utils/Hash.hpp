#pragma once

#include <functional>

namespace Atlas {

    // from: https://stackoverflow.com/a/57595105
    template <typename T, typename... Rest>
    void hash(std::size_t& seed, const T& v, const Rest&... rest) {
        seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        (hash(seed, rest), ...);
    };

}