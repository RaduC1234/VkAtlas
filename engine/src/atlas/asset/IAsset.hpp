#pragma once
#include <cstddef>
#include <cstdint>

namespace Atlas {
    class IAsset {
    public:
        explicit IAsset(const uint64_t hash) : hash(hash) {}

        virtual ~IAsset() = default;

        size_t getHash() const {
            return hash;
        }

    protected:
        const uint64_t hash{0};
    };
}
