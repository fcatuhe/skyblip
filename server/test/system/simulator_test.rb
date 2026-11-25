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

  # Lockdown Mode on iPhone and some hardened browsers have no WebAssembly, and the simulator failed there on every page that shows it.
  test "a browser with no WebAssembly keeps the device at rest, with no error" do
    assert Release.local_simulator, "Run make -C firmware browser first"
    page.driver.browser.page.evaluate_on_new_document "delete window.WebAssembly"

    visit "/try"
    page.document.synchronize(8) do
      raise Capybara::ExpectationNotMet, "the simulator has not connected" unless page.evaluate_script(<<~JS)
        Boolean(document.querySelector(".simulator") && Stimulus.getControllerForElementAndIdentifier(document.querySelector(".simulator"), "simulator"))
      JS
    end
    started = page.evaluate_async_script(<<~JS)
      const done = arguments[arguments.length - 1]
      Stimulus.getControllerForElementAndIdentifier(document.querySelector(".simulator"), "simulator").start()
        .then(() => done("started"), error => done(error.message))
    JS

    assert_equal "started", started
    assert_no_selector ".simulator--running"
    assert_selector ".simulator-placeholder"
  end

  test "the home simulator waits for the frame to scroll into view and can pair with Bravo" do
    assert Release.local_simulator, "Run make -C firmware browser first"

    page.current_window.resize_to(1000, 700)
    page.driver.browser.cookies.set(name: "locale", value: "fr", domain: page.server.host, path: "/")

    using_wait_time(8) do
      visit "/fr"
      frame = find("#simulator_build[loading=lazy]")
      assert_no_selector "#simulator_build .simulator", visible: :all, wait: 0
      assert_equal 0, page.evaluate_script("performance.getEntriesByName(document.querySelector('#simulator_build').src).length")

      scroll_to frame, align: :center
      assert_selector "#simulator_build[complete] .simulator--running"
      assert_unchecked_field "Avec Bravo (skyBlip Tango)"
      assert_equal [ "go", "unpaired", 0, 1 ], first_arrivals(1)

      check "Avec Bravo (skyBlip Tango)"
      assert_selector "#simulator_build .simulator--running"
      assert_equal [ "paired", "live", 1, 2 ], first_arrivals(2)
    end
  ensure
    page.current_window.resize_to(*ApplicationSystemTestCase::SCREEN_SIZE)
  end

  test "arrow keys fly the home simulator while the pointer is over it, and scroll the page otherwise" do
    assert Release.local_simulator, "Run make -C firmware browser first"

    using_wait_time(8) do
      visit "/"
      scroll_to find("#simulator_build[loading=lazy]"), align: :center
      device = find("#simulator_build[complete] .simulator--running .sb-case")
      scroll_to device, align: :center
      climb = find("#simulator_build [data-axis=climb]", visible: :all)
      initial_climb = climb.value

      point_away_from_simulator
      assert_arrow_down_scrolls
      assert_equal initial_climb, climb.value

      device.hover
      scroll = page.evaluate_script("window.scrollY")
      page.driver.browser.keyboard.type(:down)
      assert_operator climb.value.to_f, :>, initial_climb.to_f
      assert_equal scroll, page.evaluate_script("window.scrollY")

      point_away_from_simulator
      flown_climb = climb.value
      assert_arrow_down_scrolls
      assert_equal flown_climb, climb.value
    end
  end

  test "arrow keys fly the home simulator while its device has focus, wherever the pointer is" do
    assert Release.local_simulator, "Run make -C firmware browser first"

    using_wait_time(8) do
      visit "/"
      scroll_to find("#simulator_build[loading=lazy]"), align: :center
      device = find("#simulator_build[complete] .simulator--running .sb-case")
      scroll_to device, align: :center
      climb = find("#simulator_build [data-axis=climb]", visible: :all)
      initial_climb = climb.value

      device.send_keys(:down)
      assert_selector ".sb-case:focus-visible"
      assert_not_equal "none", page.evaluate_script("getComputedStyle(document.activeElement).outlineStyle")
      assert_operator climb.value.to_f, :>, initial_climb.to_f

      point_away_from_simulator
      flown_climb = climb.value
      page.driver.browser.keyboard.type(:down)
      assert_operator climb.value.to_f, :>, flown_climb.to_f
    end
  end

  # A press on the pad or the device button left the focus on the pad, and the arrow keys stopped flying.
  test "arrow keys still fly after a press on the pad or the device button" do
    assert Release.local_simulator, "Run make -C firmware browser first"

    visit "/try"
    using_wait_time(8) { assert_selector ".simulator--running" }
    climb = find("[data-axis=climb]", visible: :all)

    find(".sb-pad").click
    point_away_from_simulator
    climb_before = climb.value.to_f
    page.driver.browser.keyboard.type(:down)
    assert_operator climb.value.to_f, :>, climb_before

    find(".sb-key--action").click
    point_away_from_simulator
    climb_before = climb.value.to_f
    page.driver.browser.keyboard.type(:down)
    assert_operator climb.value.to_f, :>, climb_before
  end

  test "a focused dial keeps its own arrow keys while the pointer is over the simulator" do
    assert Release.local_simulator, "Run make -C firmware browser first"

    visit "/try"
    using_wait_time(8) { assert_selector ".simulator--running" }
    speed = find("[data-axis=speed]", visible: :all)
    turn = find("[data-axis=turn]", visible: :all)
    initial_speed = speed.value
    initial_turn = turn.value

    find(".sb-case").hover
    page.execute_script("document.querySelector('[data-axis=speed]').focus()")
    page.driver.browser.keyboard.type(:right)

    assert_operator speed.value.to_f, :>, initial_speed.to_f
    assert_equal initial_turn, turn.value
  end

  private
    def point_away_from_simulator
      page.driver.browser.mouse.move(x: 5, y: 5)
      assert_no_selector ".simulator:hover"
    end

    def assert_arrow_down_scrolls
      scroll = page.evaluate_script("window.scrollY")
      page.driver.browser.keyboard.type(:down)
      page.document.synchronize do
        raise Capybara::ExpectationNotMet, "Arrow Down did not scroll the page" unless page.evaluate_script("window.scrollY") > scroll
      end
    end

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
