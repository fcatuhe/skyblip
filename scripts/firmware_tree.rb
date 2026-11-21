#!/usr/bin/env ruby
require "json"
require_relative "../lib/firmware_tree"

ARGV => [ query, firmware ]
tree = FirmwareTree.new(firmware)
case query
in "version" then puts tree.version
in "products" then puts JSON.generate(tree.products)
end
