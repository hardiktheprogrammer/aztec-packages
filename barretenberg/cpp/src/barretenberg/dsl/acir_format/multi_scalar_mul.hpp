#pragma once
#include "barretenberg/dsl/types.hpp"
#include "barretenberg/serialize/msgpack.hpp"
#include <cstdint>

namespace acir_format {

struct MultiScalarMul {
    uint32_t low;
    uint32_t high;
    uint32_t pub_key_x;
    uint32_t pub_key_y;

    // for serialization, update with any new fields
    MSGPACK_FIELDS(low, high, pub_key_x, pub_key_y);
    friend bool operator==(MultiScalarMul const& lhs, MultiScalarMul const& rhs) = default;
};

template <typename Builder> void create_multi_scalar_mul_constraint(Builder& builder, const MultiScalarMul& input);

} // namespace acir_format
