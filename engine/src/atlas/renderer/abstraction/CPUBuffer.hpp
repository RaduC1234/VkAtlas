#pragma once
#include <any>

namespace Atlas {
    class CPUBuffer {
    public:
        class Builder {
        public:
            Builder& setSize(size_t size) {
                size_ = size;
                return *this;
            }

            Builder& setInitialData(std::any data) {
                data_ = std::move(data);
                return *this;
            }

            CPUBuffer build() {
                CPUBuffer buf;
                buf.size_ = size_;
                buf.storage_ = std::move(data_);
                return buf;
            }

        private:
            size_t size_ = 0;
            std::any data_;
        };

        template<typename T>
        T& as() { return std::any_cast<T&>(storage_); }

        template<typename T>
        const T& as() const { return std::any_cast<const T&>(storage_); }

        size_t size() const { return size_; }

    private:
        std::any storage_;
        size_t size_ = 0;
    };
}