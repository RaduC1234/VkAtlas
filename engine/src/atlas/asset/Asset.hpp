#pragma once
#include <cstdint>
#include <string>

namespace Atlas {
    /**
     * @brief Abstract base class for all loadable assets
     *
     * Assets are identified by a content hash computed from their data.
     * This enables automatic deduplication - assets with identical content
     * share the same hash and can reuse the same GPU resources.
     */
    class Asset {
    public:
        virtual ~Asset() = default;

        /**
         * @brief Get the content hash of this asset
         * Hash is computed lazily on first access and then cached
         * @return 64-bit content hash
         */
        size_t getHash() const {
            return hash;
        }

        void setHash(const size_t hash) {
            this->hash = hash;
        }

        /**
         * @brief Get the virtual path of this asset
         * @return Virtual path string (e.g., "models/scene.glb#mesh0")
         */
        const std::string& getVirtualPath() const {
            return virtualPath;
        }

        /**
         * @brief Set the virtual path for this asset
         * @param path Virtual path string
         */
        void setVirtualPath(const std::string& path) {
            virtualPath = path;
        }

    protected:

        std::string virtualPath;
        uint64_t hash{0};  // 0 = not yet computed
    };
} // namespace Atlas