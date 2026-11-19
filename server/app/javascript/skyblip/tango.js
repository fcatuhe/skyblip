// INFO: fc 17nov26 a Go paired with a Bravo is a Tango, and either page offers the pairing (README.md, Pairing from the briefing)
export const PARTNER = { go: 'bravo', bravo: 'go' };
const VERDICT_WITHIN_MS = 60_000;
const READ_BACK_WITHIN_MS = 5_000;

export function tagText(tag) {
  return tag === null || tag === undefined ? null : tag.toString(16).toUpperCase().padStart(4, '0');
}

function partnersOf(unit, units) {
  const product = PARTNER[unit.product];
  return units.filter(other => other !== unit && other.product === product && (other.remembered || other.linked));
}

export function tangoView(unit, units) {
  const state = unit?.state;
  if (!unit?.linked || !state?.settings || !PARTNER[unit.product] || unit.tag === null || unit.needsActivation) return null;
  if (state.status?.flight === 'airborne') return null;
  const partners = partnersOf(unit, units);
  const linked = partners.filter(other => other.linked && other.tag !== null && other.state?.settings);
  const pairedTag = state.pairedTag;
  if (pairedTag !== null && pairedTag !== undefined) {
    const current = partners.find(other => other.tag === pairedTag) ?? null;
    const other = linked.find(each => each.tag !== pairedTag);
    if (other && !(current?.linked)) return { kind: 'replace', partner: other, pairedTag };
    return { kind: 'paired', partner: current, pairedTag };
  }
  const partner = linked[0];
  if (partner) return { kind: 'pair', partner, replaces: partner.state.pairedTag ?? null };
  const known = partners.find(other => !other.linked && other.remembered);
  if (known) return { kind: 'connect', partner: known };
  return { kind: 'none' };
}

export function tangoGroups(units) {
  const sorted = [...units].sort(byKind);
  const gos = sorted.filter(unit => unit.product === 'go');
  const goFor = new Map(sorted.filter(unit => unit.product === 'bravo').map(bravo => [bravo, goOf(bravo, gos)]));
  return sorted.filter(unit => !goFor.get(unit)).map(unit => ({ unit, partners: sorted.filter(bravo => goFor.get(bravo) === unit) }));
}

const isTag = tag => tag !== null && tag !== undefined;

// INFO: fc 19nov26 either side's paired_tag ties a Bravo to its Go, since this browser may have read only one of the two
function goOf(bravo, gos) {
  return gos.find(go => isTag(bravo.pairedTag) && go.tag === bravo.pairedTag)
    ?? gos.find(go => isTag(go.pairedTag) && go.pairedTag === bravo.tag)
    ?? null;
}

function byKind(one, other) {
  return rankOf(one) - rankOf(other)
    || Number(one.slug.startsWith('activate-')) - Number(other.slug.startsWith('activate-'))
    || (numberOf(one) - numberOf(other) || 0)
    || one.slug.localeCompare(other.slug);
}

function rankOf(unit) {
  if (unit.product === 'go') return unit.plus ? 1 : 0;
  return unit.product === 'bravo' ? 2 : 3;
}

function numberOf(unit) {
  return Number.parseInt(unit.slug.split('-').at(-1), 16);
}

export function sides(unit, partner) {
  return unit.product === 'go' ? { go: unit, bravo: partner } : { go: partner, bravo: unit };
}

export class PairingFailed extends Error {
  constructor(side, key, detail = null) {
    super(`${side} ${key}`);
    Object.assign(this, { side, key, detail });
  }
}

function until(fleet, predicate, withinMs) {
  return new Promise((resolve, reject) => {
    let stop = () => {};
    const timer = setTimeout(() => {
      stop();
      reject(new PairingFailed('page', 'timeout'));
    }, withinMs);
    const check = () => {
      const value = predicate();
      if (!value) return;
      clearTimeout(timer);
      stop();
      resolve(value);
    };
    stop = fleet.listen(check);
    check();
  });
}

function settled(unit) {
  const state = unit.state;
  if (!unit.linked) return { key: 'link_lost' };
  if (state.task === 'pair') return null;
  return state.notice?.during === 'pair' ? state.notice : { key: 'refused' };
}

// INFO: fc 17nov26 Bravo first, so its acceptance stands armed before the Go's request reaches it | an untrusted Bravo waits for its press
export async function pairTango({ fleet, go, bravo, onStep = () => {}, withinMs = VERDICT_WITHIN_MS, readBackWithinMs = READ_BACK_WITHIN_MS }) {
  const pressBravo = bravo.state?.trust !== 'trusted';
  if (!pressBravo) {
    onStep('arming');
    bravo.updater.pair(go.tag);
    const armed = await until(fleet, () => settled(bravo), withinMs);
    if (armed.key !== 'pair_armed') throw new PairingFailed('bravo', armed.key, armed.detail);
  }
  const waiting = pressBravo ? 'press_bravo' : 'pairing';
  onStep(waiting);
  go.updater.pair(bravo.tag);
  const verdict = await until(fleet, () => {
    if (go.state?.phase === 'confirming' && go.state.task === 'pair') onStep('confirm_go');
    else if (go.state?.task === 'pair') onStep(waiting);
    return settled(go);
  }, withinMs);
  if (verdict.key !== 'paired') throw new PairingFailed('go', verdict.key, verdict.detail);
  bravo.updater.readSettings();
  await until(fleet, () => bravo.state?.pairedTag === go.tag, readBackWithinMs).catch(error => {
    console.warn('Bravo did not read back the Go it paired with:', error.message);
  });
  onStep('paired');
}
