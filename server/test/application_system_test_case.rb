require "test_helper"
require "capybara/cuprite"

class ApplicationSystemTestCase < ActionDispatch::SystemTestCase
  driven_by :cuprite,
    screen_size: [ 1400, 1400 ],
    options: {
      process_timeout: 30,
      url_blacklist: [ %r{\Ahttps?://(?!127\.0\.0\.1|localhost)} ]
    }
end
