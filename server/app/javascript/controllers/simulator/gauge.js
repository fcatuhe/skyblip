const laws = new WeakMap()

export function lawPosition({ points, cycle }, value) {
  const at = cycle ? ((value % cycle) + cycle) % cycle : value
  const [first, last] = [points[0], points.at(-1)]
  if (at <= first[0]) return first[1]
  if (at >= last[0]) return last[1]
  const next = points.findIndex(([edge]) => edge >= at)
  const [[fromValue, fromPosition], [toValue, toPosition]] = [points[next - 1], points[next]]
  return fromPosition + ((at - fromValue) * (toPosition - fromPosition)) / (toValue - fromValue)
}

export function movePart(part, value) {
  if (!laws.has(part)) laws.set(part, JSON.parse(part.dataset.law))
  const law = laws.get(part)
  const position = lawPosition(law, value)
  part.setAttribute("transform", law.move === "slide" ? `translate(${position}, 0)` : `rotate(${position})`)
  for (const numeral of part.querySelectorAll(".gauge-numeral"))
    numeral.setAttribute("transform", `rotate(${-position}, ${numeral.getAttribute("x")}, ${numeral.getAttribute("y")})`)
}
