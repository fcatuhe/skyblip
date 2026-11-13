#!/usr/bin/env ruby
require "json"
require_relative "../lib/radio_protocols"

ARGV => [ firmware, version, products ]
print RadioProtocols.new(firmware).notes(version:, products: JSON.parse(products))
