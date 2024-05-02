use dep::std;
// This test checks that we perform dead-instruction-elimination on intrinsic functions.
fn main(x: Field) {
    let hash = std::hash::pedersen_commitment([x]);
    let _p1 = std::scalar_mul::multi_scalar_mul_embedded_curve(x, 0);
}
