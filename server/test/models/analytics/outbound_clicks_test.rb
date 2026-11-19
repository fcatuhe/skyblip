require "test_helper"
require_relative "../../test_helpers/analytics_test_helper"

class Analytics::OutboundClicksTest < ActiveSupport::TestCase
  include AnalyticsTestHelper

  GO = "https://link.amazon/B00EaKI0W"
  GO_PLUS = "https://link.amazon/B05es09aX"

  setup do
    clear_analytics
    @pilot = browse [ [ "/buy", 30 ] ], visitor: "pilot", started_at: Time.utc(2026, 1, 10, 9)
    @other_pilot = browse [ [ "/buy", 30 ] ], visitor: "other-pilot", started_at: Time.utc(2026, 1, 12, 9)
  end

  test "links count the clicks and the visitors of each link that leaves the site, most clicked first" do
    click @pilot, GO, text: "€68", section: "go"
    click @pilot, GO, text: "€68", section: "go", after: 5.seconds
    click @other_pilot, GO, text: "€68", section: "go"
    click @other_pilot, GO_PLUS, text: "€96", section: "go-plus"

    links = Analytics::OutboundClicks.new(visits).links

    assert_equal [ [ GO, "go", "€68", 3, 2 ], [ GO_PLUS, "go-plus", "€96", 1, 1 ] ],
      links.map { it.to_h.values_at(:href, :section, :text, :clicks, :visitors) }
    assert_equal [ Time.utc(2026, 1, 10, 9, 0, 1), Time.utc(2026, 1, 12, 9, 0, 1) ], [ links.first.first_at, links.first.last_at ]
  end

  test "a link to the site itself, or that is no web address, is not outbound" do
    click @pilot, "https://skyblip.eu/install"
    click @pilot, "https://www.skyblip.eu/guides"
    click @pilot, "mailto:contact@skyblip.eu"
    click @pilot, "https://github.com/fcatuhe/skyblip"

    assert_equal [ "https://github.com/fcatuhe/skyblip" ], Analytics::OutboundClicks.new(visits).links.map(&:href)
  end

  test "matching keeps the links whose address holds the words, and the visits keep their filters" do
    click @pilot, GO
    click @pilot, "https://www.lilygo.cc/skyblip-go"
    click @other_pilot, GO_PLUS

    assert_equal [ GO, GO_PLUS ], Analytics::OutboundClicks.new(visits, matching: "amazon").links.map(&:href).sort
    assert_equal [ GO ], Analytics::OutboundClicks.new(visits(start_on: "2026-01-10", end_on: "2026-01-10"), matching: "amazon").links.map(&:href)
  end

  test "each lists the clicks oldest first, with the visit they came in" do
    click @other_pilot, GO_PLUS, text: "€96", section: "go-plus"
    click @pilot, GO, text: "€68", section: "go"

    clicks = Analytics::OutboundClicks.new(visits).each(limit: 10)

    assert_equal [ [ GO, "pilot", "non_user", "/buy" ], [ GO_PLUS, "other-pilot", "non_user", "/buy" ] ],
      clicks.map { it.to_h.values_at(:href, :visitor_token, :segment, :entry_path) }
    assert_equal [ GO_PLUS ], Analytics::OutboundClicks.new(visits).each(limit: 1).map(&:href)
  end

  private
    def visits(start_on: "2026-01-01", end_on: "2026-01-31")
      Analytics::Visit.filtered(start_on:, end_on:)
    end
end
