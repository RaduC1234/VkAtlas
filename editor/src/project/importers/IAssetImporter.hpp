#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "project/importers/EntityBuffer.hpp"

namespace Atlas::Editor {
    class IAssetImporter {
    public:
        virtual ~IAssetImporter() = default;
        virtual std::vector<std::string> extensions() const = 0;

        virtual void importAsset(const std::string &path, EntityBuffer &buffer) = 0;
    };
}
