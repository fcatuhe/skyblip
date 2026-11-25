require "test_helper"

class Briefing::HandoffTest < ActiveSupport::TestCase
  test "a handoff names the product installed and the tag the unit will advertise" do
    handoff = Briefing::Handoff.from(installed: "go", tag: "3F2A")

    assert_equal "go", handoff.product
    assert_equal({ installed: "go", tag: "3f2a" }, handoff.to_query)
  end

  test "a tag that is no tag is dropped, and a product that is none is no handoff" do
    assert_equal({ installed: "bravo" }, Briefing::Handoff.from(installed: "bravo", tag: "<script>").to_query)
    assert_nil Briefing::Handoff.from(installed: "router")
    assert_nil Briefing::Handoff.from({})
  end
end
