#!/usr/bin/env ruby
require_relative "../lib/deploy_order"

ARGV => [ commit ]
root = File.expand_path("..", __dir__)
order = DeployOrder.new(commit:, running: DeployOrder.running_version(File.join(root, "server")), repository: root)
warn "Production runs #{order.running}, newer than #{commit}: this run leaves it in place." unless order.deploy?
puts "deploy=#{order.deploy?}"
