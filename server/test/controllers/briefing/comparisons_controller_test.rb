require "test_helper"

class Briefing::ComparisonsControllerTest < ActionDispatch::IntegrationTest
  test "the comparison offers each receiver on its own transport, and the chart around Merignac" do
    get briefing_comparison_path

    assert_response :success
    assert_select "[data-controller=comparison][data-comparison-tiles-value*='SCAN-OACI']", count: 1
    assert_select "button[data-comparison-source-param=retro][data-comparison-transport-param=serial]", text: "Connect"
    assert_select "button[data-comparison-source-param=bravo][data-comparison-transport-param=bluetooth]", text: "Connect"
    assert_select "select[data-comparison-target=baud] option[value='38400']", count: 1
    assert_select "input[name=latitude][value='44.8283']"
    assert_select "input[name=radius][value='100']"
    assert_select "main script, main style, main [style], main [onclick], main [oninput]", count: 0
  end
end
