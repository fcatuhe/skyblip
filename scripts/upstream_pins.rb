#!/usr/bin/env ruby
require_relative "../lib/upstream_pins"

pins = UpstreamPins.new(File.expand_path("..", __dir__))
case ARGV
in [ "version", name ]
  puts pins.version(name)
in [ "check" ]
  pins.mismatches.each { |mismatch| warn "FAIL: #{mismatch}" }
  if pins.mismatches.empty?
    warn "OK: the vendored doctest and firmware/west.yml match #{UpstreamPins::PINS}."
  else
    exit 1
  end
end
