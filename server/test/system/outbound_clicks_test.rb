require "application_system_test_case"

class OutboundClicksTest < ApplicationSystemTestCase
  setup { Ahoy.api_only = false }
  teardown { Ahoy.api_only = true }

  test "a click and a middle click on an affiliate link are both recorded, with the product of their row" do
    visit "/buy"

    within "#where" do
      click_link "£52"
      middle_click find_link("£71", href: "https://link.amazon/B0g84Docp")
    end

    assert_clicked [ [ "https://link.amazon/B02Dnj0kb", "go", "£52" ], [ "https://link.amazon/B0g84Docp", "go-plus", "£71" ] ]
  end

  private
    def middle_click(link)
      link.execute_script("this.dispatchEvent(new MouseEvent('auxclick', { button: 1, bubbles: true }))")
    end

    def assert_clicked(links)
      page.document.synchronize do
        clicked = Ahoy::Event.where(name: "$click").order(:id).map { it.props.values_at(:href, :section, :text) }
        raise Capybara::ExpectationNotMet, "clicked #{clicked}" unless clicked == links
      end
    end
end
