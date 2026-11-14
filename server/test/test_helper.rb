ENV["RAILS_ENV"] ||= "test"
require_relative "../config/environment"
require "rails/test_help"
require_relative "test_helpers/session_test_helper"
require_relative "test_helpers/activation_test_helper"
require "webmock/minitest"
require "vcr"

require_relative "test_helpers/vcr_config"

# INFO: fc 14nov26 Mission Control's English-only I18n config drops French when its request loads translations first, as CI's eager load prevents
I18n.eager_load!

module ActiveSupport
  class TestCase
    parallelize(workers: :number_of_processors)

    fixtures :all
  end
end
