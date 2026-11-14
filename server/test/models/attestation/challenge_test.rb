require "test_helper"

class Attestation::ChallengeTest < ActiveSupport::TestCase
  test "a server nonce is spent once" do
    challenge = Attestation::Challenge.create!
    found = Attestation::Challenge.find_live(challenge.server_nonce)

    assert_match Activation::NONCE, challenge.server_nonce
    assert_equal challenge, Attestation::Challenge.find_live(challenge.server_nonce).spend
    assert_equal :expired, assert_raises(Attestation::Refusal) { found.spend }.reason
    assert_equal :expired, assert_raises(Attestation::Refusal) { Attestation::Challenge.find_live(challenge.server_nonce) }.reason
  end

  test "a server nonce expires after five minutes" do
    challenge = Attestation::Challenge.create!

    travel 5.minutes + 1.second do
      assert_equal :expired, assert_raises(Attestation::Refusal) { Attestation::Challenge.find_live(challenge.server_nonce) }.reason
      assert_includes Attestation::Challenge.expired, challenge
    end
  end
end
