#include "IGPUResource.hpp"

#include <cassert>

#include "GPUCubemap.hpp"
#include "GPUMesh.hpp"
#include "GPUTexture.hpp"

namespace Atlas {
    std::unique_ptr<GPUTexture> IGPUResource::defaultTexture_;
    std::unique_ptr<GPUCubemap> IGPUResource::defaultCubemap_;
    std::unique_ptr<GPUMesh> IGPUResource::defaultMesh_;

    template<>
    GPUTexture &IGPUResource::default_<GPUTexture>() {
        assert(defaultTexture_ && "Default GPUTexture was not created");
        return *defaultTexture_;
    }

    template<>
    GPUCubemap &IGPUResource::default_<GPUCubemap>() {
        assert(defaultCubemap_ && "Default GPUCubemap was not created");
        return *defaultCubemap_;
    }

    template<>
    GPUMesh &IGPUResource::default_<GPUMesh>() {
        assert(defaultMesh_ && "Default GPUMesh was not created");
        return *defaultMesh_;
    }

    template<>
    void IGPUResource::createDefault<GPUTexture>(Device &device) {
        if (!defaultTexture_)
            defaultTexture_ = GPUTexture::createDefault(device);
    }

    template<>
    void IGPUResource::createDefault<GPUCubemap>(Device &device) {
        if (!defaultCubemap_)
            defaultCubemap_ = GPUCubemap::createDefault(device);
    }

    template<>
    void IGPUResource::createDefault<GPUMesh>(Device &device) {
        if (!defaultMesh_)
            defaultMesh_ = GPUMesh::createDefault(device);
    }

    void IGPUResource::destroyDefaults() {
        defaultMesh_.reset();
        defaultCubemap_.reset();
        defaultTexture_.reset();
    }
}
