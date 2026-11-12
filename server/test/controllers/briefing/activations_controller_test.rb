require "test_helper"

class Briefing::ActivationsControllerTest < ActionDispatch::IntegrationTest
  test "activation opens on the one thing to do" do
    get briefing_activation_path

    assert_response :success
    assert_select "h1", text: "Activate a new Go"
    assert_select "button", text: "Find the new Go"
    assert_select "[data-activation-challenges-value='/activation/challenges']"
  end
end
