class Release::SimulatorFiles < Data.define(:loader, :wasm, :embed, :scenarios)
  CURRENT = new(loader: "skyblip-simulator.js", wasm: "skyblip-simulator.wasm", embed: "skyblip-simulator-embed.js",
                scenarios: "skyblip-simulator-scenarios.json")
  # TODO: fc 25nov26 v0.8.0 and the builds before it carry these names: delete LEGACY once the release after v0.8.0 ships (#483)
  LEGACY = new(loader: "skyblip_simulator.js", wasm: "skyblip_simulator.wasm", embed: "embed.js", scenarios: "scenarios.json")
  NAMINGS = [ CURRENT, LEGACY ].freeze

  def self.in(assets) = NAMINGS.find { |naming| naming.held_in?(assets) }

  def runtime = [ loader, wasm, embed ]
  def names = [ *runtime, scenarios ]
  def held_in?(assets) = (runtime - assets).empty?
end
