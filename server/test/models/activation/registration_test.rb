require "test_helper"

class Activation::RegistrationTest < ActiveSupport::TestCase
  setup do
    @challenge = Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key_id: "890026b1")
  end

  test "takes true or false as release, and nothing else, an array of them included" do
    assert Activation::Registration.new(signed_registration(@challenge, release: true)).valid?
    assert Activation::Registration.new(signed_registration(@challenge, release: false)).valid?

    [ [], [ true ], [ false ], [ true, false ], "true", 1, nil ].each do |release|
      assert_not Activation::Registration.new(signed_registration(@challenge).merge(release:)).valid?, release.inspect
    end
  end
end
