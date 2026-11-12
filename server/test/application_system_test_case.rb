require "test_helper"
require "capybara/cuprite"

require_relative "test_helpers/briefing_test_helper"

class ApplicationSystemTestCase < ActionDispatch::SystemTestCase
  SCREEN_SIZE = [ 1400, 1400 ].freeze

  include BriefingTestHelper

  driven_by :cuprite,
    screen_size: SCREEN_SIZE,
    options: {
      process_timeout: 30,
      url_blacklist: [ %r{\Ahttps?://(?!127\.0\.0\.1|localhost)} ]
    }
end
