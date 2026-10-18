require "test_helper"

class Activation::ChallengesControllerTest < ActionDispatch::IntegrationTest
  test "answers the unit's nonce with a server nonce and its signature over both" do
    post activation_challenges_path, params: { unit_nonce: vectors["unit_nonce"], server_key: "890026b1" }, as: :json

    assert_response :created
    server_nonce, signature = response.parsed_body.values_at("server_nonce", "signature")
    assert server_verifies?(signature, Identity::Message.register(unit_nonce: vectors["unit_nonce"], server_nonce:))
    assert Activation::Challenge.exists?(server_nonce:, unit_nonce: vectors["unit_nonce"])
  end

  test "names what it refuses" do
    post activation_challenges_path, params: { unit_nonce: vectors["unit_nonce"], server_key: "a9649d2d" }, as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "unknown_server_key" }, response.parsed_body)

    post activation_challenges_path, params: { unit_nonce: "not hex", server_key: "890026b1" }, as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)
  end
end
