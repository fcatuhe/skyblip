require "test_helper"

class Attestation::ChallengesControllerTest < ActionDispatch::IntegrationTest
  test "answers a server nonce it keeps for the statement that follows" do
    post attestation_challenges_path, as: :json

    assert_response :created
    server_nonce = response.parsed_body.fetch("server_nonce")
    assert_match Activation::NONCE, server_nonce
    assert_equal [ "server_nonce" ], response.parsed_body.keys
    assert Attestation::Challenge.exists?(server_nonce:)
  end
end
