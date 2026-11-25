class Release::SimulatorFiles < Data.define(:loader, :wasm, :embed, :scenarios)
  CURRENT = new(loader: "skyblip-simulator.js", wasm: "skyblip-simulator.wasm", embed: "skyblip-simulator-embed.js",
                scenarios: "skyblip-simulator-scenarios.json")

  def self.in(assets) = (CURRENT if CURRENT.held_in?(assets))

  def runtime = [ loader, wasm, embed ]
  def names = [ *runtime, scenarios ]
  def held_in?(assets) = (runtime - assets).empty?
end
