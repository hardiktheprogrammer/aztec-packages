#include "eccvm_prover.hpp"
#include "barretenberg/commitment_schemes/claim.hpp"
#include "barretenberg/commitment_schemes/commitment_key.hpp"
#include "barretenberg/common/ref_array.hpp"
#include "barretenberg/honk/proof_system/logderivative_library.hpp"
#include "barretenberg/honk/proof_system/permutation_library.hpp"
#include "barretenberg/plonk_honk_shared/library/grand_product_library.hpp"
#include "barretenberg/polynomials/polynomial.hpp"
#include "barretenberg/relations/lookup_relation.hpp"
#include "barretenberg/relations/permutation_relation.hpp"
#include "barretenberg/sumcheck/sumcheck.hpp"

namespace bb {

ECCVMProver::ECCVMProver(CircuitBuilder& builder, const std::shared_ptr<Transcript>& transcript)
    : transcript(transcript)
{
    BB_OP_COUNT_TIME_NAME("ECCVMProver(CircuitBuilder&)");

    // TODO(https://github.com/AztecProtocol/barretenberg/issues/939): Remove redundancy between
    // ProvingKey/ProverPolynomials and update the model to reflect what's done in all other proving systems.

    // Construct the proving key; populates all polynomials except for witness polys
    key = std::make_shared<ProvingKey>(builder);

    commitment_key = std::make_shared<CommitmentKey>(key->circuit_size);
}

/**
 * @brief Add circuit size, public input size, and public inputs to transcript
 *
 */
void ECCVMProver::execute_preamble_round()
{
    const auto circuit_size = static_cast<uint32_t>(key->circuit_size);

    transcript->send_to_verifier("circuit_size", circuit_size);
}

/**
 * @brief Compute commitments to the first three wires
 *
 */
void ECCVMProver::execute_wire_commitments_round()
{
    auto wire_polys = key->polynomials.get_wires();
    auto labels = commitment_labels.get_wires();
    for (size_t idx = 0; idx < wire_polys.size(); ++idx) {
        transcript->send_to_verifier(labels[idx], commitment_key->commit(wire_polys[idx]));
    }
}

/**
 * @brief Compute sorted witness-table accumulator
 *
 */
void ECCVMProver::execute_log_derivative_commitments_round()
{
    // Compute and add beta to relation parameters
    auto [beta, gamma] = transcript->template get_challenges<FF>("beta", "gamma");

    // TODO(#583)(@zac-williamson): fix Transcript to be able to generate more than 2 challenges per round! oof.
    auto beta_sqr = beta * beta;
    relation_parameters.gamma = gamma;
    relation_parameters.beta = beta;
    relation_parameters.beta_sqr = beta_sqr;
    relation_parameters.beta_cube = beta_sqr * beta;
    relation_parameters.eccvm_set_permutation_delta =
        gamma * (gamma + beta_sqr) * (gamma + beta_sqr + beta_sqr) * (gamma + beta_sqr + beta_sqr + beta_sqr);
    relation_parameters.eccvm_set_permutation_delta = relation_parameters.eccvm_set_permutation_delta.invert();
    // Compute inverse polynomial for our logarithmic-derivative lookup method
    compute_logderivative_inverse<Flavor, typename Flavor::LookupRelation>(
        key->polynomials, relation_parameters, key->circuit_size);
    transcript->send_to_verifier(commitment_labels.lookup_inverses,
                                 commitment_key->commit(key->polynomials.lookup_inverses));
}

/**
 * @brief Compute permutation and lookup grand product polynomials and commitments
 *
 */
void ECCVMProver::execute_grand_product_computation_round()
{
    // Compute permutation grand product and their commitments
    compute_grand_products<Flavor>(key->polynomials, relation_parameters);

    transcript->send_to_verifier(commitment_labels.z_perm, commitment_key->commit(key->polynomials.z_perm));
}

/**
 * @brief Run Sumcheck resulting in u = (u_1,...,u_d) challenges and all evaluations at u being calculated.
 *
 */
void ECCVMProver::execute_relation_check_rounds()
{
    using Sumcheck = SumcheckProver<Flavor>;

    auto sumcheck = Sumcheck(key->circuit_size, transcript);
    FF alpha = transcript->template get_challenge<FF>("Sumcheck:alpha");
    std::vector<FF> gate_challenges(numeric::get_msb(key->circuit_size));
    for (size_t idx = 0; idx < gate_challenges.size(); idx++) {
        gate_challenges[idx] = transcript->template get_challenge<FF>("Sumcheck:gate_challenge_" + std::to_string(idx));
    }
    sumcheck_output = sumcheck.prove(key->polynomials, relation_parameters, alpha, gate_challenges);
}

/**
 * @brief Execute the ZeroMorph protocol to prove the multilinear evaluations produced by Sumcheck
 * @details See https://hackmd.io/dlf9xEwhTQyE3hiGbq4FsA?view for a complete description of the unrolled protocol.
 *
 * */
void ECCVMProver::execute_zeromorph_rounds()
{
    ZeroMorph::prove(key->polynomials.get_unshifted(),
                     key->polynomials.get_to_be_shifted(),
                     sumcheck_output.claimed_evaluations.get_unshifted(),
                     sumcheck_output.claimed_evaluations.get_shifted(),
                     sumcheck_output.challenge,
                     commitment_key,
                     transcript);
}

/**
 * @brief Batch open the transcript polynomials as univariates for Translator consistency check
 * TODO(#768): Find a better way to do this. See issue for details.
 *
 * @tparam Flavor
 */
void ECCVMProver::execute_transcript_consistency_univariate_opening_round()
{
    // Since IPA cannot currently handle polynomials for which the latter half of the coefficients are 0, we hackily
    // batch the constant polynomial 1 in with the 5 transcript polynomials. See issue #768 for more details.
    Polynomial hack(key->circuit_size);
    for (size_t idx = 0; idx < key->circuit_size; idx++) {
        hack[idx] = 1;
    }
    transcript->send_to_verifier("Translation:hack_commitment", commitment_key->commit(hack));

    // Get the challenge at which we evaluate the polynomials as univariates
    evaluation_challenge_x = transcript->template get_challenge<FF>("Translation:evaluation_challenge_x");

    translation_evaluations.op = key->polynomials.transcript_op.evaluate(evaluation_challenge_x);
    translation_evaluations.Px = key->polynomials.transcript_Px.evaluate(evaluation_challenge_x);
    translation_evaluations.Py = key->polynomials.transcript_Py.evaluate(evaluation_challenge_x);
    translation_evaluations.z1 = key->polynomials.transcript_z1.evaluate(evaluation_challenge_x);
    translation_evaluations.z2 = key->polynomials.transcript_z2.evaluate(evaluation_challenge_x);

    // Add the univariate evaluations to the transcript
    transcript->send_to_verifier("Translation:op", translation_evaluations.op);
    transcript->send_to_verifier("Translation:Px", translation_evaluations.Px);
    transcript->send_to_verifier("Translation:Py", translation_evaluations.Py);
    transcript->send_to_verifier("Translation:z1", translation_evaluations.z1);
    transcript->send_to_verifier("Translation:z2", translation_evaluations.z2);
    transcript->send_to_verifier("Translation:hack_evaluation", hack.evaluate(evaluation_challenge_x));

    // Get another challenge for batching the univariate claims
    FF ipa_batching_challenge = transcript->template get_challenge<FF>("Translation:ipa_batching_challenge");

    // Collect the polynomials and evaluations to be batched
    RefArray univariate_polynomials{ key->polynomials.transcript_op, key->polynomials.transcript_Px,
                                     key->polynomials.transcript_Py, key->polynomials.transcript_z1,
                                     key->polynomials.transcript_z2, hack };
    std::array<FF, univariate_polynomials.size()> univariate_evaluations;
    for (auto [eval, polynomial] : zip_view(univariate_evaluations, univariate_polynomials)) {
        eval = polynomial.evaluate(evaluation_challenge_x);
    }

    // Construct the batched polynomial and batched evaluation
    Polynomial batched_univariate{ key->circuit_size };
    FF batched_evaluation{ 0 };
    auto batching_scalar = FF(1);
    for (auto [polynomial, eval] : zip_view(univariate_polynomials, univariate_evaluations)) {
        batched_univariate.add_scaled(polynomial, batching_scalar);
        batched_evaluation += eval * batching_scalar;
        batching_scalar *= ipa_batching_challenge;
    }

    // TODO(https://github.com/AztecProtocol/barretenberg/issues/922): We are doing another round of IPA here with
    // exactly the same labels and no domain separation so if/when labels are going to matter we are clashing.
    PCS::compute_opening_proof(
        commitment_key, { evaluation_challenge_x, batched_evaluation }, batched_univariate, transcript);

    // Get another challenge for batching the univariate claims
    translation_batching_challenge_v = transcript->template get_challenge<FF>("Translation:batching_challenge");
}

HonkProof& ECCVMProver::export_proof()
{
    proof = transcript->export_proof();
    return proof;
}

HonkProof& ECCVMProver::construct_proof()
{
    BB_OP_COUNT_TIME_NAME("ECCVMProver::construct_proof");

    execute_preamble_round();

    execute_wire_commitments_round();

    execute_log_derivative_commitments_round();

    execute_grand_product_computation_round();

    execute_relation_check_rounds();

    execute_zeromorph_rounds();

    execute_transcript_consistency_univariate_opening_round();

    return export_proof();
}
} // namespace bb
