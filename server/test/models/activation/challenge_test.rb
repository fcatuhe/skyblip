require "test_helper"

class Activation::ChallengeTest < ActiveSupport::TestCase
  test "signs the unit's nonce and its own with the key the unit trusts" do
    challenge = Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key: "890026b1", ip_address: "192.0.2.9")

    assert_match Activation::NONCE, challenge.server_nonce
    message = Activation::Message.register(unit_nonce: vectors["unit_nonce"], server_nonce: challenge.server_nonce)
    assert server_verifies?(challenge.signature, message)
  end

  test "refuses a malformed nonce and a key it does not hold" do
    assert_equal :invalid, assert_raises(Activation::Refusal) { Activation::Challenge.issue(unit_nonce: "ABC", server_key: "890026b1") }.reason
    assert_equal :unknown_server_key, assert_raises(Activation::Refusal) { Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key: "00000000") }.reason
  end

  test "a server nonce is redeemed once" do
    challenge = Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key: "890026b1")

    assert_equal challenge, Activation::Challenge.redeem(challenge.server_nonce)
    assert_equal :expired, assert_raises(Activation::Refusal) { Activation::Challenge.redeem(challenge.server_nonce) }.reason
  end

  test "a server nonce expires after five minutes" do
    challenge = Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key: "890026b1")

    travel 5.minutes + 1.second do
      assert_equal :expired, assert_raises(Activation::Refusal) { Activation::Challenge.redeem(challenge.server_nonce) }.reason
      assert_includes Activation::Challenge.expired, challenge
    end
  end
end
