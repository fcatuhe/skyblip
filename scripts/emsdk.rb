#!/usr/bin/env ruby
require_relative "../lib/emsdk"
require_relative "../lib/upstream_pins"

ARGV => [ candidate ]
version = UpstreamPins.new(File.expand_path("..", __dir__)).version("emscripten-core/emsdk")
directory = ENV.fetch("EMSDK", File.join(Dir.home, ".local/opt/emsdk"))
puts "#{version} #{Emsdk.emcc(candidate:, version:, directory:)}"
