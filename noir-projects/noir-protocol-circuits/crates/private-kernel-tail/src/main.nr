use dep::private_kernel_lib::PrivateKernelTailCircuitPrivateInputs;
use dep::types::KernelCircuitPublicInputs;

fn main(input: PrivateKernelTailCircuitPrivateInputs) ->  pub KernelCircuitPublicInputs {
    input.native_private_kernel_circuit_tail()
}
