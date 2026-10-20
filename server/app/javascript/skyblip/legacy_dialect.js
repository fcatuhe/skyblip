import TABLE from '#skyblip/legacy_dialect_table';

export const LINK = TABLE.link;
export const SIMULATOR = TABLE.simulator;

export function current(section, frame) {
  let read = respelled(renamed(frame, section.keys), section.values);
  for (const entry of section.frames ?? []) {
    if (applies(section, entry, read)) read = respelled(renamed(read, entry.keys), entry.values);
  }
  return read;
}

export function legacy(section, frame) {
  const entries = (section.frames ?? []).filter(entry => applies(section, entry, frame));
  const written = entries.reduceRight((out, entry) => renamed(respelled(out, inverted(entry.values)), inverse(entry.keys)), frame);
  return renamed(respelled(written, inverted(section.values)), inverse(section.keys));
}

export function currentSimulator(simulator) {
  for (const [old, name] of Object.entries(SIMULATOR.keys)) {
    if (Object.hasOwn(simulator, old) && !Object.hasOwn(simulator, name)) simulator[name] = simulator[old];
  }
  return simulator;
}

export class Dialect {
  #legacy = null;

  receive(frame) {
    if (this.#legacy === null) {
      if (LINK.detect.legacy.some(key => Object.hasOwn(frame, key))) this.#legacy = true;
      else if (LINK.detect.current.some(key => Object.hasOwn(frame, key))) this.#legacy = false;
    }
    return current(LINK, frame);
  }

  send(frame) {
    if (this.#legacy === null) return { ...legacy(LINK, frame), ...frame };
    return this.#legacy ? legacy(LINK, frame) : frame;
  }
}

function applies(section, entry, frame) {
  return entry.when.includes(frame[section.select]) &&
    Object.entries(entry.where ?? {}).every(([key, held]) => held.includes(frame[key]));
}

function renamed(frame, keys = {}) {
  return Object.fromEntries(Object.entries(frame).map(([key, value]) => [Object.hasOwn(keys, key) ? keys[key] : key, value]));
}

function respelled(frame, values = {}) {
  const out = { ...frame };
  for (const [key, spellings] of Object.entries(values)) {
    if (typeof out[key] === 'string' && Object.hasOwn(spellings, out[key])) out[key] = spellings[out[key]];
  }
  return out;
}

function inverse(map = {}) {
  return Object.fromEntries(Object.entries(map).map(([old, name]) => [name, old]));
}

function inverted(values = {}) {
  return Object.fromEntries(Object.entries(values).map(([key, spellings]) => [key, inverse(spellings)]));
}
