#!/usr/bin/env ruby
require_relative "../lib/release_draft"

ARGV => [ files ]
ReleaseDraft.new(tag: ENV.fetch("GITHUB_REF_NAME"), repository: ENV.fetch("GH_REPO")).create(files)
