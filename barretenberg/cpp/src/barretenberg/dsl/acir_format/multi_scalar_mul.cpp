#include "multi_scalar_mul.hpp"
#include "barretenberg/dsl/types.hpp"
#include "barretenberg/ecc/curves/bn254/fr.hpp"
#include "barretenberg/ecc/curves/grumpkin/grumpkin.hpp"
#include "barretenberg/plonk_honk_shared/arithmetization/gate_data.hpp"

namespace acir_format {

template <typename Builder> void create_multi_scalar_mul_constraint(Builder& builder, const MultiScalarMul& input)
{
    using cycle_group_ct = bb::stdlib::cycle_group<Builder>;
    using cycle_scalar_ct = typename bb::stdlib::cycle_group<Builder>::cycle_scalar;
    using field_ct = bb::stdlib::field_t<Builder>;

    // Weinitialize output_point because we do not have identity element available in cycle_group_ct
    cycle_group_ct output_point;

    // We iterate over points and scalars in the input, we multiply each pair and then add the output point of the
    // iteration to the output point
    // Note: We iterate by 2 because each point and scalar is represented by 2 witness indices ([x, y] for point and
    // [low, high] for scalar)
    for (size_t i = 0; i < input.points.size(); i += 2) {
        // We instantiate the input point/variable base as `cycle_group_ct`
        auto point_x = field_ct::from_witness_index(&builder, input.points[i]);
        auto point_y = field_ct::from_witness_index(&builder, input.points[i + 1]);
        cycle_group_ct input_point(point_x, point_y, false);

        // We reconstruct the scalar from the low and high limbs
        field_ct scalar_low_as_field = field_ct::from_witness_index(&builder, input.scalars[i]);
        field_ct scalar_high_as_field = field_ct::from_witness_index(&builder, input.scalars[i + 1]);
        cycle_scalar_ct scalar(scalar_low_as_field, scalar_high_as_field);

        // We multiply input point with the scalar to get the output point of this iteration
        auto iteration_output_point = input_point * scalar;

        if (i == 0) {
            // If this is the first iteration, we assign the result to the output point
            output_point = iteration_output_point;
        } else {
            // If this is not the first iteration, we add the result to the output point
            output_point = output_point + iteration_output_point;
        }
    }

    // Finally we add the constraints
    builder.assert_equal(output_point.x.get_witness_index(), input.out_point_x);
    builder.assert_equal(output_point.y.get_witness_index(), input.out_point_y);
}

template void create_multi_scalar_mul_constraint<UltraCircuitBuilder>(UltraCircuitBuilder& builder,
                                                                      const MultiScalarMul& input);
template void create_multi_scalar_mul_constraint<GoblinUltraCircuitBuilder>(GoblinUltraCircuitBuilder& builder,
                                                                            const MultiScalarMul& input);

} // namespace acir_format
