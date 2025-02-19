import {
  AztecAddress,
  CallRequest,
  GasSettings,
  MAX_PUBLIC_CALL_STACK_LENGTH_PER_TX,
  Nullifier,
  PartialPrivateTailPublicInputsForPublic,
  PrivateKernelTailCircuitPublicInputs,
  Proof,
  type PublicCallRequest,
  SideEffect,
  computeContractClassId,
  getContractClassFromArtifact,
} from '@aztec/circuits.js';
import {
  makeCombinedAccumulatedData,
  makeCombinedConstantData,
  makePublicCallRequest,
} from '@aztec/circuits.js/testing';
import { type ContractArtifact } from '@aztec/foundation/abi';
import { makeTuple } from '@aztec/foundation/array';
import { times } from '@aztec/foundation/collection';
import { randomBytes } from '@aztec/foundation/crypto';
import { Fr } from '@aztec/foundation/fields';
import { type ContractInstanceWithAddress, SerializableContractInstance } from '@aztec/types/contracts';

import { EncryptedL2Log } from './logs/encrypted_l2_log.js';
import { EncryptedFunctionL2Logs, EncryptedTxL2Logs, Note, UnencryptedTxL2Logs } from './logs/index.js';
import { ExtendedNote } from './notes/index.js';
import { type ProcessOutput, type ProcessReturnValues, SimulatedTx, Tx, TxHash } from './tx/index.js';

/**
 * Testing utility to create empty logs composed from a single empty log.
 */
export function makeEmptyLogs(): EncryptedTxL2Logs {
  const functionLogs = [new EncryptedFunctionL2Logs([EncryptedL2Log.empty()])];
  return new EncryptedTxL2Logs(functionLogs);
}

export const randomTxHash = (): TxHash => new TxHash(randomBytes(32));

export const mockTx = (
  seed = 1,
  {
    hasLogs = false,
    numberOfNonRevertiblePublicCallRequests = MAX_PUBLIC_CALL_STACK_LENGTH_PER_TX / 2,
    numberOfRevertiblePublicCallRequests = MAX_PUBLIC_CALL_STACK_LENGTH_PER_TX / 2,
    publicCallRequests = [],
  }: {
    hasLogs?: boolean;
    numberOfNonRevertiblePublicCallRequests?: number;
    numberOfRevertiblePublicCallRequests?: number;
    publicCallRequests?: PublicCallRequest[];
  } = {},
) => {
  const totalPublicCallRequests =
    numberOfNonRevertiblePublicCallRequests + numberOfRevertiblePublicCallRequests || publicCallRequests.length;
  if (publicCallRequests.length && publicCallRequests.length !== totalPublicCallRequests) {
    throw new Error(
      `Provided publicCallRequests does not match the required number of call requests. Expected ${totalPublicCallRequests}. Got ${publicCallRequests.length}`,
    );
  }

  const isForPublic = totalPublicCallRequests > 0;
  const data = PrivateKernelTailCircuitPublicInputs.empty();
  const firstNullifier = new Nullifier(new Fr(seed + 1), 0, Fr.ZERO);
  const encryptedLogs = hasLogs ? EncryptedTxL2Logs.random(2, 3) : EncryptedTxL2Logs.empty(); // 2 priv function invocations creating 3 encrypted logs each
  const unencryptedLogs = hasLogs ? UnencryptedTxL2Logs.random(2, 1) : UnencryptedTxL2Logs.empty(); // 2 priv function invocations creating 1 unencrypted log each
  data.constants.txContext.gasSettings = GasSettings.default();

  if (isForPublic) {
    data.forRollup = undefined;
    data.forPublic = PartialPrivateTailPublicInputsForPublic.empty();

    data.forPublic.endNonRevertibleData.newNullifiers[0] = firstNullifier;

    publicCallRequests = publicCallRequests.length
      ? publicCallRequests.slice().sort((a, b) => b.callContext.sideEffectCounter - a.callContext.sideEffectCounter)
      : times(totalPublicCallRequests, i => makePublicCallRequest(seed + 0x100 + i));

    data.forPublic.endNonRevertibleData.publicCallStack = makeTuple(MAX_PUBLIC_CALL_STACK_LENGTH_PER_TX, i =>
      i < numberOfNonRevertiblePublicCallRequests
        ? publicCallRequests[numberOfRevertiblePublicCallRequests + i].toCallRequest()
        : CallRequest.empty(),
    );
    if (hasLogs) {
      let i = 1; // 0 used in first nullifier
      encryptedLogs.functionLogs.forEach((log, j) => {
        // ts complains if we dont check .forPublic here, even though it is defined ^
        if (data.forPublic) {
          data.forPublic.end.encryptedLogsHashes[j] = new SideEffect(Fr.fromBuffer(log.hash()), new Fr(i++));
        }
      });
      unencryptedLogs.functionLogs.forEach((log, j) => {
        if (data.forPublic) {
          data.forPublic.end.unencryptedLogsHashes[j] = new SideEffect(Fr.fromBuffer(log.hash()), new Fr(i++));
        }
      });
    }

    data.forPublic.end.publicCallStack = makeTuple(MAX_PUBLIC_CALL_STACK_LENGTH_PER_TX, i =>
      i < numberOfRevertiblePublicCallRequests ? publicCallRequests[i].toCallRequest() : CallRequest.empty(),
    );
  } else {
    data.forRollup!.end.newNullifiers[0] = firstNullifier.value;
    data.forRollup!.end.encryptedLogsHash = Fr.fromBuffer(encryptedLogs.hash());
    data.forRollup!.end.unencryptedLogsHash = Fr.fromBuffer(unencryptedLogs.hash());
  }

  const tx = new Tx(data, new Proof(Buffer.alloc(0)), encryptedLogs, unencryptedLogs, publicCallRequests);

  return tx;
};

export const mockTxForRollup = (seed = 1, { hasLogs = false }: { hasLogs?: boolean } = {}) =>
  mockTx(seed, { hasLogs, numberOfNonRevertiblePublicCallRequests: 0, numberOfRevertiblePublicCallRequests: 0 });

export const mockSimulatedTx = (seed = 1, hasLogs = true) => {
  const tx = mockTx(seed, { hasLogs });
  const dec: ProcessReturnValues = [new Fr(1n), new Fr(2n), new Fr(3n), new Fr(4n)];
  const output: ProcessOutput = {
    constants: makeCombinedConstantData(),
    encryptedLogs: tx.encryptedLogs,
    unencryptedLogs: tx.unencryptedLogs,
    end: makeCombinedAccumulatedData(),
    revertReason: undefined,
    publicReturnValues: dec,
  };
  return new SimulatedTx(tx, dec, output);
};

export const randomContractArtifact = (): ContractArtifact => ({
  name: randomBytes(4).toString('hex'),
  functions: [],
  outputs: {
    structs: {},
    globals: {},
  },
  fileMap: {},
  storageLayout: {},
  notes: {},
});

export const randomContractInstanceWithAddress = (opts: { contractClassId?: Fr } = {}): ContractInstanceWithAddress =>
  SerializableContractInstance.random(opts).withAddress(AztecAddress.random());

export const randomDeployedContract = () => {
  const artifact = randomContractArtifact();
  const contractClassId = computeContractClassId(getContractClassFromArtifact(artifact));
  return { artifact, instance: randomContractInstanceWithAddress({ contractClassId }) };
};

export const randomExtendedNote = ({
  note = Note.random(),
  owner = AztecAddress.random(),
  contractAddress = AztecAddress.random(),
  txHash = randomTxHash(),
  storageSlot = Fr.random(),
  noteTypeId = Fr.random(),
}: Partial<ExtendedNote> = {}) => {
  return new ExtendedNote(note, owner, contractAddress, storageSlot, noteTypeId, txHash);
};
