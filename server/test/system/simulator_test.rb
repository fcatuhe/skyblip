require "application_system_test_case"

class SimulatorTest < ApplicationSystemTestCase
  test "the public simulator relays extra traffic through Bravo only when paired" do
    assert Release.local_simulator, "Run make -C firmware browser first"

    visit "/try"
    using_wait_time(8) { assert_unchecked_field "With Bravo (skyBlip Tango)" }
    assert_equal [ "go", "unpaired", 0, 1 ], first_arrivals(1)

    check "With Bravo (skyBlip Tango)"
    assert_checked_field "With Bravo (skyBlip Tango)"
    assert_equal [ "paired", "live", 1, 2 ], first_arrivals(2)
    assert_text "Screen: Radar"

    uncheck "With Bravo (skyBlip Tango)"
    assert_unchecked_field "With Bravo (skyBlip Tango)"
    assert_equal [ "go", "unpaired", 0, 1 ], first_arrivals(1)
  end

  private
    def first_arrivals(expected)
      page.document.synchronize do
        raise Capybara::ExpectationNotMet, "Simulator has not booted" unless page.evaluate_script(<<~JS)
          (() => {
            const sim = Stimulus.getControllerForElementAndIdentifier(document.querySelector(".simulator"), "simulator")?.sim
            if (!sim) return false
            sim.advance(3000)
            return true
          })()
        JS
      end

      page.document.synchronize do
        count = page.evaluate_script(<<~JS)
          Stimulus.getControllerForElementAndIdentifier(document.querySelector(".simulator"), "simulator").sim.aircraftCount()
        JS
        raise Capybara::ExpectationNotMet, "Expected #{expected} arrivals, got #{count}" unless count == expected
      end

      page.evaluate_async_script(<<~JS)
        const done = arguments[arguments.length - 1]
        const { sim, srcValue } = Stimulus.getControllerForElementAndIdentifier(document.querySelector(".simulator"), "simulator")
        import(srcValue).then(({ PRODUCT, BRAVO_STATE }) => {
          sim.advance(12000)
          done([PRODUCT[sim.product()], BRAVO_STATE[sim.bravoState()], sim.adsbAircraft(), sim.traffic()])
        }, error => done(error.message))
      JS
    end
end
