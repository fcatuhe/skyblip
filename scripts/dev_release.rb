#!/usr/bin/env ruby
require_relative "../lib/dev_release"

ARGV => [ images, simulator, source ]
DevRelease.publish_all(images:, simulator:, source:, ref: ENV.fetch("GITHUB_REF_NAME"), sha: ENV.fetch("GITHUB_SHA"),
                       repository: ENV.fetch("GH_REPO"))
