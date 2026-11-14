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

  STIMULUS_CONNECTED = <<~JS.freeze
    window.Stimulus !== undefined && [ ...document.querySelectorAll("[data-controller]") ].every(element =>
      element.dataset.controller.split(/\s+/).filter(Boolean).every(identifier =>
        Stimulus.getControllerForElementAndIdentifier(element, identifier) !== null))
  JS

  # INFO: fc 14nov26 Stimulus imports the controllers after the load event, so a test acting at once meets a page that ignores it
  def visit(...)
    super
    page.document.synchronize do
      raise Capybara::ExpectationNotMet, "Stimulus has not connected every controller" unless page.evaluate_script(STIMULUS_CONNECTED)
    end
  end
end
