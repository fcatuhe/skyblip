// INFO: fc 12nov26 a reading of core/comms/log_link.cpp: list, read windows, and the chunk sizes it plans
const RECORD_BYTES = 24;
const MAX_RECORDS_PER_CHUNK = 19;
const MAX_READ_CHUNKS = 8;
const CHUNK_ENVELOPE_BYTES = 18;
const STORE_FIELD_BYTES = 2;

const headBytes = length => (length < 24 ? 1 : length < 256 ? 2 : 3);

export function recordsPerChunk(payloadBytes) {
  for (let records = MAX_RECORDS_PER_CHUNK; records > 0; records--) {
    const raw = records * RECORD_BYTES;
    if (CHUNK_ENVELOPE_BYTES + STORE_FIELD_BYTES + headBytes(raw) + raw <= payloadBytes) return records;
  }
  return 0;
}

export class FakeLogStore {
  constructor({ sessions = [], refuse = null, silent = false, stray = null } = {}) {
    this.sessions = sessions;
    this.refuse = refuse;
    this.silent = silent;
    this.stray = stray;
    this.reads = [];
  }

  answer(request, payloadBytes) {
    if (this.silent) return [];
    const replies = this.reply(request, payloadBytes);
    if (this.stray && request.command === 'read') {
      replies.unshift(this.stray);
      this.stray = null;
    }
    return replies;
  }

  reply({ command, log = 'flights', index, session, from = 0, count = 1 }, payloadBytes) {
    if (this.refuse) return [this.refusal(log, this.refuse)];
    const held = log === 'diagnostics' ? this.sessions : [];
    if (command === 'list' && index === undefined) return [{ command: 'log', log: this.named(log), acknowledge: true, sessions: held.length }];
    if (command === 'list') return held[index] ? [this.entry(log, held, index)] : [this.refusal(log, 'no_session')];
    if (command !== 'read') return [this.refusal(log, 'unknown_command')];
    this.reads.push({ from, count });
    const entry = held.find(each => each.session === session);
    if (!entry) return [this.refusal(log, 'no_session')];
    const perChunk = recordsPerChunk(payloadBytes);
    if (perChunk === 0) return [this.refusal(log, 'small_mtu')];
    return this.window(log, entry, from, Math.min(Math.max(count, 1), MAX_READ_CHUNKS), perChunk);
  }

  window(log, { session, records }, from, count, perChunk) {
    const total = records.length / RECORD_BYTES;
    if (from >= total) return [{ command: 'chunk', log: this.named(log), session, from, ends_session: true, records: new Uint8Array(0) }];
    const chunks = [];
    for (let at = from; at < total && chunks.length < count; at += perChunk) {
      const end = Math.min(at + perChunk, total);
      chunks.push({
        command: 'chunk', log: this.named(log), session, from: at, ends_session: end >= total,
        records: records.slice(at * RECORD_BYTES, end * RECORD_BYTES),
      });
    }
    return chunks;
  }

  entry(log, held, index) {
    const { session, records, closed = true, truncated = false } = held[index];
    return { command: 'session', log: this.named(log), index, of: held.length, session, records: records.length / RECORD_BYTES, closed, truncated };
  }

  refusal(log, reason) {
    return { command: 'log', log: this.named(log), acknowledge: false, reason };
  }

  named(log) {
    return log === 'diagnostics' ? log : undefined;
  }
}
