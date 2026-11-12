import { decodeChunk, sessionLines } from '#skyblip/diagnostics';

// INFO: fc 12nov26 comms::kMaxLogReadChunks, the clamp a read's count meets on the device
export const LOG_READ_CHUNKS_MAX = 8;
const REPLY_TIMEOUT_MS = 5000;
const LOG = 'diagnostics';

export class LogError extends Error {
  constructor(message, reason = null) {
    super(message);
    this.reason = reason;
  }
}

export class LogReader {
  #send;
  #timeoutMs;
  #frames = [];
  #waiting = [];

  constructor(send, { timeoutMs = REPLY_TIMEOUT_MS } = {}) {
    this.#send = send;
    this.#timeoutMs = timeoutMs;
  }

  receive(frame, bytes = null, error = null) {
    const reply = frame ? { frame } : { error: new LogError(`the unit sent a frame outside the registry (${error?.message})`) };
    const waiter = this.#waiting.shift();
    if (waiter) waiter(reply);
    else this.#frames.push(reply);
  }

  close() {
    for (const waiter of this.#waiting.splice(0)) waiter({ error: new LogError('the link closed') });
    this.#frames = [];
  }

  async ask(fields) {
    this.#frames = [];
    await this.#send(fields);
  }

  // INFO: fc 12nov26 the console keeps its link between fetches, so a chunk of a window abandoned earlier can still arrive
  async next(command, isOurs = () => true) {
    for (;;) {
      const reply = this.#frames.shift() ?? (await this.#arrival());
      if (reply.error) throw reply.error;
      const { frame } = reply;
      if (frame.acknowledge === false) throw new LogError(`the unit refused: ${frame.reason ?? 'no reason given'}`, frame.reason ?? null);
      if (frame.command === command && frame.log === LOG && isOurs(frame)) return frame;
    }
  }

  #arrival() {
    return new Promise(resolve => {
      const timer = setTimeout(() => {
        this.#waiting.splice(this.#waiting.indexOf(waiter), 1);
        resolve({ error: new LogError(`no reply on the log endpoint within ${this.#timeoutMs / 1000} s`) });
      }, this.#timeoutMs);
      const waiter = reply => {
        clearTimeout(timer);
        resolve(reply);
      };
      this.#waiting.push(waiter);
    });
  }
}

export async function fetchDiagnostics(reader, { onProgress = () => {} } = {}) {
  await reader.ask({ command: 'list', log: LOG });
  const { sessions = 0 } = await reader.next('log');
  if (sessions === 0) return null;
  await reader.ask({ command: 'list', log: LOG, index: sessions - 1 });
  const session = await reader.next('session', entry => entry.index === sessions - 1);
  const records = await readSession(reader, session, onProgress);
  return { session, lines: sessionLines(session, records) };
}

async function readSession(reader, session, onProgress) {
  const records = [];
  let total = session.records;
  let at = 0;
  while (at < total) {
    await reader.ask({ command: 'read', log: LOG, session: session.session, from: at, count: LOG_READ_CHUNKS_MAX });
    for (let chunks = 0; chunks < LOG_READ_CHUNKS_MAX; chunks++) {
      const chunk = await reader.next('chunk', ({ session: id, from }) => id === session.session && from === at);
      if (chunk.records.length === 0 && !chunk.ends_session) {
        throw new LogError(`session ${session.session} was asked for index ${at} and answered a chunk from ${chunk.from}`);
      }
      const decoded = decodeChunk(chunk.records, at);
      records.push(...decoded);
      at += decoded.length;
      if (chunk.ends_session) total = at;
      onProgress(at, total);
      if (chunk.ends_session) break;
    }
  }
  return records;
}
