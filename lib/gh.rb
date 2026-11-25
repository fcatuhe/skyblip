require "open3"

module Gh
  def self.run(*arguments)
    output, status = Open3.capture2("gh", *arguments)
    raise "gh #{arguments.first(2).join(" ")} failed: #{status}" unless status.success?

    output
  end
end
