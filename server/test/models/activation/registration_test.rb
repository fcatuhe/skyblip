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

  test "takes a tag from 0 to 65535 as an integer, and nothing else, an array of one included" do
    [ 0, 0xFFFF ].each do |bluetooth_tag|
      assert Activation::Registration.new(signed_registration(@challenge).merge(bluetooth_tag:)).valid?, bluetooth_tag.inspect
    end

    [ -1, 0x10000, "16170", [ 16170 ], 16170.0, nil ].each do |bluetooth_tag|
      assert_not Activation::Registration.new(signed_registration(@challenge).merge(bluetooth_tag:)).valid?, bluetooth_tag.inspect
    end
  end

  test "names the unit whole, or not at all as a registration of v1 does" do
    assert Activation::Registration.new(signed_registration_v1(@challenge)).valid?

    %i[product variant bluetooth_tag].each do |left_out|
      assert_not Activation::Registration.new(signed_registration(@challenge).except(left_out)).valid?, left_out
    end
    assert_not Activation::Registration.new(signed_registration(@challenge).merge(product: "glider")).valid?
    assert_not Activation::Registration.new(signed_registration(@challenge).merge(variant: "max")).valid?
  end
end
