import { compareVersions, parseVersion } from "#skyblip/image"

export function changesSince(sections, { channel, running, offered }) {
  const from = parseVersion(running)
  const to = parseVersion(offered)
  if (!from || !to) return []
  return sections.filter(section => {
    const version = parseVersion(section.dataset.version)
    return section.dataset.channel === channel && version && compareVersions(version, from) > 0 && compareVersions(version, to) <= 0
  })
}
