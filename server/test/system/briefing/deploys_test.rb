require "application_system_test_case"

class Briefing::DeploysTest < ApplicationSystemTestCase
  test "a deploy while a device is connected renders the next page in place, and the next one after the link closes loads anew" do
    in_range a_go
    on_the_shelf do
      connect_device
      mark_the_document

      deploying_new_scripts do
        visit_by_turbo "/briefing/devices/go-00012a/firmware"
        assert_current_path "/briefing/devices/go-00012a/firmware"
        assert_selector ".fleet-unit.is-linked", text: "Go 00012A"
        assert_equal true, page.evaluate_script("window.markedDocument")

        visit_by_turbo "/briefing/devices/go-00012a"
        find(".unit-menu summary").click
        click_button "Disconnect"
        assert_selector ".unit-facts", text: "Not connected"

        visit_by_turbo "/briefing"
        assert_reloaded
      end
    end
  end

  test "a page left open over a deploy loads anew when it comes back to the foreground" do
    in_range a_go
    on_the_shelf do
      visit "/briefing"
      mark_the_document

      deploying_new_scripts do
        page.execute_script("document.dispatchEvent(new Event('visibilitychange'))")
        assert_reloaded
      end
    end
  end

  # A home page restored with the scripts of a morning deploy drew the simulator frame of the afternoon's, and its controller missed a target.
  test "a frame answered after a deploy loads the page anew, so old scripts never meet new markup" do
    page.current_window.resize_to(1000, 700)
    visit "/"
    frame = find("#simulator_build[loading=lazy]")
    mark_the_document

    deploying_new_scripts do
      scroll_to frame, align: :center
      assert_reloaded
    end
  ensure
    page.current_window.resize_to(*ApplicationSystemTestCase::SCREEN_SIZE)
  end

  # Turbo prefetches a link on hover, and a prefetch answered by a deploy once loaded the hovered page.
  test "a link hovered after a deploy is prefetched, and the page stays where it is" do
    in_range a_go
    on_the_shelf do
      visit "/briefing"
      page.execute_script(<<~JS)
        addEventListener("turbo:before-fetch-response", event => {
          if (event.target.tagName === "A") window.prefetchHandled = event.defaultPrevented
        })
      JS

      deploying_new_scripts do
        find(".app-menu summary").click
        within(".app-menu") { find_link("Set up a device").hover }
        page.document.synchronize do
          raise Capybara::ExpectationNotMet, "no prefetch answered" if page.evaluate_script("window.prefetchHandled").nil?
        end
        assert_equal false, page.evaluate_script("window.prefetchHandled")
        assert_current_path "/briefing"
      end
    end
  end

  private
    def mark_the_document = page.execute_script("window.markedDocument = true")

    def visit_by_turbo(path) = page.execute_script("Turbo.visit(#{path.to_json})")

    def assert_reloaded
      page.document.synchronize do
        raise Capybara::ExpectationNotMet, "the page was not loaded anew" if page.evaluate_script("window.markedDocument")
      end
    end

    # INFO: fc 25nov26 a pin changes the import map, as a deploy with new scripts does, and pinning again before the delete clears its cache
    def deploying_new_scripts
      Rails.application.importmap.pin "deployed", to: "application.js", preload: false
      yield
    ensure
      Rails.application.importmap.pin "deployed", to: "application.js", preload: false
      Rails.application.importmap.packages.delete("deployed")
    end
end
