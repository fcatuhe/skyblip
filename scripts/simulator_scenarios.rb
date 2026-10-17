#!/usr/bin/env ruby
require_relative "../lib/simulator_scenarios"

puts SimulatorScenarios.generate(ARGV.fetch(0) { File.expand_path("../firmware/scenarios", __dir__) })
