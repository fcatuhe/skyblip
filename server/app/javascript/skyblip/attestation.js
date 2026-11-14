import { ServerRefusal, postJson, relayed } from '#skyblip/activation';
import { Replies } from '#skyblip/replies';

const ANSWER_WITHIN_MS = 5_000;
// INFO: fc 14nov26 the unit refuses attest with these alone, so an earlier command's refusal is not taken for its answer
const REFUSALS = new Set(['in_flight', 'keyless', 'small_mtu', 'invalid', 'unknown_command', 'claimed']);

const answers = reply => reply.command === 'attestation' || (reply.command === 'acknowledgement' && REFUSALS.has(reply.reason));

function csrfToken() {
  return globalThis.document?.querySelector('meta[name="csrf-token"]')?.content ?? null;
}

// INFO: fc 14nov26 no support code goes with it, so the server ties the firmware to the unit and to no browser
export function attestationServer({ challenges = '/attestation/challenges', statements = '/attestation/statements', token = csrfToken } = {}) {
  return {
    challenge: () => postJson(challenges, {}, { token: token() }),
    attest: statement => postJson(statements, statement, { token: token() }),
  };
}

export class FirmwareAttestation {
  #server;
  #replies;

  constructor({ send, server }) {
    this.#server = server;
    this.#replies = new Replies({ send, withinMs: ANSWER_WITHIN_MS });
  }

  // INFO: fc 14nov26 claimed also reaches the updater, which tells the pilot another app holds the unit
  receive(reply) {
    return this.#replies.take(reply) && reply.reason !== 'claimed';
  }

  close() {
    this.#replies.close();
  }

  async attest() {
    this.#replies.open();
    const challenge = await recorded(() => this.#server.challenge());
    if (!challenge) return null;
    const reply = await this.#replies.ask({ command: 'attest', server_nonce: challenge.server_nonce }, answers);
    if (reply?.command !== 'attestation') return null;
    return recorded(() => this.#server.attest({ ...relayed(reply), server_nonce: challenge.server_nonce }));
  }
}

async function recorded(request) {
  try {
    return await request();
  } catch (error) {
    if (!(error instanceof ServerRefusal)) throw error;
    console.warn('The server did not record the firmware this unit signed:', error.message);
    return null;
  }
}
