import { type BlockProver } from './block-prover.js';
import { type ProvingJobSource } from './proving-job.js';

/**
 * The prover configuration.
 */
export type ProverConfig = {
  /** The working directory to use for simulation/proving */
  acvmWorkingDirectory: string;
  /** The path to the ACVM binary */
  acvmBinaryPath: string;
  /** The working directory to for proving */
  bbWorkingDirectory: string;
  /** The path to the bb binary */
  bbBinaryPath: string;
  /** How many agents to start */
  proverAgents: number;
  /** Enable proving. If true, must set bb env vars */
  realProofs: boolean;
  /** The interval agents poll for jobs at */
  proverAgentPollInterval: number;
};

/**
 * The interface to the prover client.
 * Provides the ability to generate proofs and build rollups.
 */
export interface ProverClient extends BlockProver {
  start(): Promise<void>;

  stop(): Promise<void>;

  getProvingJobSource(): ProvingJobSource;

  updateProverConfig(config: Partial<ProverConfig>): Promise<void>;
}
