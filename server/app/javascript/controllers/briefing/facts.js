import { compareVersions, parseVersion, versionName } from "#skyblip/image"
import { noTxMark } from "#skyblip/settings"
import { say } from "controllers/briefing/words"

const DATE = { day: "numeric", month: "short", year: "numeric" }
const DATE_LOCALES = { en: "en-GB", fr: "fr-FR", de: "de-DE", it: "it-IT", es: "es-ES" }

export function shortName(unit) {
  return unit.name.replace(/^skyBlip /, "").replace(/^activate (\w+) skyBlip$/, "Go $1")
}

export function productWord(unit) {
  return unit.product === "bravo" ? "Bravo" : "Go"
}

export function battery(status) {
  const percent = status?.batteryPercent
  if (percent === null || percent === undefined) return null
  return say(status.charging ? "words.charging" : "words.battery", { percent })
}

export function flight(status) {
  return status?.flight ? say(`words.flight.${status.flight}`) : null
}

export function running(version, track) {
  if (!version) return null
  const name = runningName(version, track)
  return track === "development" ? say("words.running", { version: name, track: say("words.track.development") }) : name
}

export function runningName(version, track) {
  return versionName(version, track === "release")
}

export function imageName(file) {
  return versionName(file.version, file.channel === "release")
}

export function protocolsLine(protocols) {
  if (!protocols?.length) return null
  return say("words.protocols", { protocols: protocols.map(({ name, version }) => `${name} ${version}`).join(", ") })
}

export function trackOf(image) {
  if (!image?.key) return null
  return image.downgrade ? "development" : "release"
}

export function seen(record) {
  if (!record.seen) return say("words.not_met")
  const lang = document.documentElement.lang
  return say("words.seen", { date: new Date(record.seen).toLocaleDateString(DATE_LOCALES[lang] ?? lang, DATE) })
}

// INFO: fc 11nov26 offline, the news is the newest image this browser saw offered, against the firmware the device last ran
export function newsOf(unit) {
  if (unit.linked) return unit.state.upgrade ? imageName(unit.state.file) : null
  const { newest, newestRelease, running: version } = unit.record
  const [a, b] = [parseVersion(newest), parseVersion(version)]
  return a && b && compareVersions(a, b) > 0 ? versionName(newest, newestRelease) : null
}

export function factsOf(unit) {
  const phase = unit.state?.phase
  if (unit.linked) {
    const { status, running: version, image } = unit.state
    return [say("words.connected"), noTxMark(status), battery(status), flight(status), running(version, trackOf(image))].filter(Boolean)
  }
  if (phase === "rebooting") return [say("words.restarting")]
  if (phase === "connecting") return [say("words.connecting")]
  return [say("words.not_connected"), seen(unit.record)]
}
