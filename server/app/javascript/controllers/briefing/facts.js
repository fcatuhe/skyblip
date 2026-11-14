import { compareVersions, parseVersion } from "#skyblip/image"
import { say } from "controllers/briefing/words"

const DATE = { day: "numeric", month: "short", year: "numeric" }
const DATE_LOCALES = { en: "en-GB", fr: "fr-FR" }

export function shortName(unit) {
  return unit.name.replace(/^skyBlip /, "").replace(/^activate (\w+) skyBlip$/, "Go $1")
}

export function productWord(unit) {
  return unit.product === "skyblip_bravo" ? "Bravo" : "Go"
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
  return track ? say("words.running", { version, track: say(`words.track.${track}`) }) : version
}

export function protocolsLine(protocols) {
  if (!protocols?.length) return null
  const named = protocols.map(({ name, version, specification }) => `${name} ${version} (${specification})`)
  return say("words.protocols", { protocols: named.join(", ") })
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
  if (unit.linked) return unit.state.upgrade ? unit.state.file.version : null
  const { newest, running: version } = unit.record
  const [a, b] = [parseVersion(newest), parseVersion(version)]
  return a && b && compareVersions(a, b) > 0 ? newest : null
}

export function factsOf(unit) {
  const phase = unit.state?.phase
  if (unit.linked) {
    const { status, running: version, image } = unit.state
    return [say("words.connected"), battery(status), flight(status), running(version, trackOf(image))].filter(Boolean)
  }
  if (phase === "rebooting") return [say("words.restarting")]
  if (phase === "connecting") return [say("words.connecting")]
  return [say("words.not_connected"), seen(unit.record)]
}
