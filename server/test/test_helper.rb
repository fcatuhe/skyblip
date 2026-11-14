ENV["RAILS_ENV"] ||= "test"
require_relative "../config/environment"
require "rails/test_help"
require_relative "test_helpers/session_test_helper"
require_relative "test_helpers/activation_test_helper"
require_relative "test_helpers/connecting_agents"
require_relative "test_helpers/configuring"
require_relative "test_helpers/caching"
require "webmock/minitest"
require "vcr"

require_relative "test_helpers/vcr_config"

module ActiveSupport
  class TestCase
    parallelize(workers: :number_of_processors)

    fixtures :all

    include Configuring
    include Caching

    def vcr(cassette, &block) = VCR.use_cassette(cassette, &block)
  end
end
