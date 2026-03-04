#pragma once
#include <entt/entity/entity.hpp>

#include "SparseSet.hpp"

template<>
struct sparse_set_traits<entt::entity> {
    static constexpr bool external_keys = true;

    using traits = entt::entt_traits<entt::entity>;

    static uint32_t index(entt::entity e) { return static_cast<uint32_t>(traits::to_entity(e)); }
    static uint32_t generation(entt::entity e) { return static_cast<uint32_t>(traits::to_version(e)); }
    static entt::entity make(uint32_t index, uint32_t gen) { return traits::construct(index, gen); }
};

template<typename T>
using Storage = SparseSet<T, entt::entity>;

