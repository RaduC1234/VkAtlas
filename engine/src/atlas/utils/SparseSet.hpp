#pragma once
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

struct sparse_id {
    uint32_t index      = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    bool operator==(const sparse_id &) const noexcept = default;
    bool operator!=(const sparse_id &) const noexcept = default;

    [[nodiscard]] bool is_null() const noexcept { return index == std::numeric_limits<uint32_t>::max(); }

    static constexpr sparse_id null() noexcept { return {}; }
};

template<typename Key = sparse_id>
struct sparse_set_traits;

template<>
struct sparse_set_traits<sparse_id> {
    static constexpr bool external_keys = false;  // SparseSet generates keys

    static uint32_t  index     (const sparse_id& id)                          { return id.index; }
    static uint32_t  generation(const sparse_id& id)                          { return id.generation; }
    static sparse_id make      (const uint32_t index, const uint32_t gen)     { return { index, gen }; }
};

template<typename T, typename Key = sparse_id, typename Allocator = std::allocator<T>>
class SparseSet {
public:
    using value_type      = T;
    using allocator_type  = Allocator;
    using size_type       = std::size_t;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer   = typename std::allocator_traits<Allocator>::const_pointer;

    using iterator               = typename std::vector<value_type>::iterator;
    using const_iterator         = typename std::vector<value_type>::const_iterator;
    using reverse_iterator       = typename std::vector<value_type>::reverse_iterator;
    using const_reverse_iterator = typename std::vector<value_type>::const_reverse_iterator;

    using traits = sparse_set_traits<Key>;


    SparseSet() = default;
    explicit SparseSet(const allocator_type& allocator) : data_(allocator) {}

    SparseSet(const SparseSet&)            = default;
    SparseSet(SparseSet&&)                 = default;
    SparseSet& operator=(const SparseSet&) = default;
    SparseSet& operator=(SparseSet&&)      = default;
    ~SparseSet()                           = default;


    [[nodiscard]] bool      empty()    const noexcept { return data_.empty(); }
    [[nodiscard]] size_type size()     const noexcept { return data_.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
    [[nodiscard]] size_type max_size() const noexcept { return data_.max_size(); }

    void reserve(size_type n) {
        data_.reserve(n);
        dense_to_slot_.reserve(n);
    }


    Key insert(Key key, const T& value) requires(traits::external_keys) { return emplace_impl(key, value); }
    Key insert(Key key, T&& value)      requires(traits::external_keys) { return emplace_impl(key, std::move(value)); }

    template<typename... Args>
    Key emplace(Key key, Args&&... args) requires(traits::external_keys) { return emplace_impl(key, std::forward<Args>(args)...); }


    Key insert(const T& value) requires(!traits::external_keys) { return emplace_impl(value); }
    Key insert(T&& value)      requires(!traits::external_keys) { return emplace_impl(std::move(value)); }

    template<typename... Args>
    Key emplace(Args&&... args) requires(!traits::external_keys) { return emplace_impl(std::forward<Args>(args)...); }


    bool erase(Key key) noexcept {
        if (!contains(key)) return false;

        const uint32_t idx        = traits::index(key);
        const uint32_t dense_idx  = sparse_[idx].dense_index;
        const uint32_t last_dense = static_cast<uint32_t>(data_.size()) - 1u;

        if (dense_idx != last_dense) {
            data_[dense_idx]          = std::move(data_[last_dense]);
            dense_to_slot_[dense_idx] = dense_to_slot_[last_dense];
            sparse_[dense_to_slot_[dense_idx]].dense_index = dense_idx;
        }

        data_.pop_back();
        dense_to_slot_.pop_back();
        ++sparse_[idx].generation;
        free_list_.push_back(idx);

        return true;
    }

    void clear() noexcept {
        data_.clear();
        dense_to_slot_.clear();
        free_list_.clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(sparse_.size()); ++i)
            free_list_.push_back(i);
    }


    [[nodiscard]] pointer find(Key key) noexcept {
        if (!contains(key)) return nullptr;
        return &data_[sparse_[traits::index(key)].dense_index];
    }

    [[nodiscard]] const_pointer find(Key key) const noexcept {
        if (!contains(key)) return nullptr;
        return &data_[sparse_[traits::index(key)].dense_index];
    }

    [[nodiscard]] reference at(Key key) {
        pointer p = find(key);
        if (!p) throw std::out_of_range("SparseSet::at — invalid key");
        return *p;
    }

    [[nodiscard]] const_reference at(Key key) const {
        const_pointer p = find(key);
        if (!p) throw std::out_of_range("SparseSet::at — invalid key");
        return *p;
    }

    [[nodiscard]] reference       operator[](Key key)       noexcept { return data_[sparse_[traits::index(key)].dense_index]; }
    [[nodiscard]] const_reference operator[](Key key) const noexcept { return data_[sparse_[traits::index(key)].dense_index]; }

    [[nodiscard]] bool contains(Key key) const noexcept {
        const uint32_t idx = traits::index(key);
        return idx < sparse_.size()
            && sparse_[idx].generation == traits::generation(key);
    }


    [[nodiscard]] pointer       data()       noexcept { return data_.data(); }
    [[nodiscard]] const_pointer data() const noexcept { return data_.data(); }


    iterator               begin()        noexcept { return data_.begin(); }
    iterator               end()          noexcept { return data_.end(); }
    const_iterator         begin()  const noexcept { return data_.begin(); }
    const_iterator         end()    const noexcept { return data_.end(); }
    const_iterator         cbegin() const noexcept { return data_.cbegin(); }
    const_iterator         cend()   const noexcept { return data_.cend(); }
    reverse_iterator       rbegin()       noexcept { return data_.rbegin(); }
    reverse_iterator       rend()         noexcept { return data_.rend(); }
    const_reverse_iterator rbegin() const noexcept { return data_.rbegin(); }
    const_reverse_iterator rend()   const noexcept { return data_.rend(); }

    [[nodiscard]] allocator_type get_allocator() const noexcept { return data_.get_allocator(); }

private:
    struct slot_entry {
        uint32_t dense_index = 0;
        uint32_t generation  = 0;
    };

    // caller owns keys
    template<typename... Args>
    Key emplace_impl(Key key, Args&&... args) requires(traits::external_keys) {
        const uint32_t idx = traits::index(key);

        if (idx >= sparse_.size())
            sparse_.resize(idx + 1);

        sparse_[idx].dense_index = static_cast<uint32_t>(data_.size());
        sparse_[idx].generation  = traits::generation(key);

        data_.emplace_back(std::forward<Args>(args)...);
        dense_to_slot_.push_back(idx);

        return key;
    }

    // generates keys
    template<typename... Args>
    Key emplace_impl(Args&&... args) requires(!traits::external_keys) {
        uint32_t slot;
        if (!free_list_.empty()) {
            slot = free_list_.back();
            free_list_.pop_back();
        } else {
            slot = static_cast<uint32_t>(sparse_.size());
            sparse_.push_back({});
        }

        sparse_[slot].dense_index = static_cast<uint32_t>(data_.size());

        data_.emplace_back(std::forward<Args>(args)...);
        dense_to_slot_.push_back(slot);

        return traits::make(slot, sparse_[slot].generation);
    }

    std::vector<T, Allocator> data_;
    std::vector<uint32_t>     dense_to_slot_;
    std::vector<slot_entry>   sparse_;
    std::vector<uint32_t>     free_list_;
};

namespace std {
    template<>
    struct hash<sparse_id> {
        std::size_t operator()(const sparse_id& id) const noexcept {
            std::size_t h = id.index;
            h ^= static_cast<std::size_t>(id.generation) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
}