#!/usr/bin/env ruby
require_relative "../lib/release_notice"

ARGV => [ source ]
print ReleaseNotice.new(source)
