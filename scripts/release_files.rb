#!/usr/bin/env ruby
require "json"
require_relative "../lib/release_files"

ARGV => [ images, simulator, source, into, version, products ]
ReleaseFiles.new(images, products: JSON.parse(products)).stage(into:, name: version, simulator:, source:, protocols: true)
