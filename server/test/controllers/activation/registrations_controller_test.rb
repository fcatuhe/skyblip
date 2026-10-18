require "test_helper"

class Activation::RegistrationsControllerTest < ActionDispatch::IntegrationTest
  setup do
    @challenge = Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key: "890026b1")
  end

  test "binds a new unit and answers its serial with an unlock code for this unit and this session" do
    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    assert_response :created
    serial, signature = response.parsed_body.values_at("serial", "signature")
    assert_equal Unit.find_by!(device_id: vectors["device_id"]).serial, serial
    message = Identity::Message.unlock(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], serial:, unit_nonce: vectors["unit_nonce"])
    assert server_verifies?(signature, message)
  end

  test "records the unit's firmware, whether it is a release, and the address it came from" do
    post activation_registrations_path, params: signed_registration(@challenge, release: false), as: :json, headers: { "REMOTE_ADDR" => "203.0.113.8" }

    key = Unit.find_by!(device_id: vectors["device_id"]).active_key
    assert_equal [ vectors["version"], "890026b1", false, "203.0.113.8" ], [ key.version, key.server_key, key.release, key.ip_address ]
  end

  test "refuses a registration that does not say whether it is a release" do
    post activation_registrations_path, params: signed_registration(@challenge).except(:release), as: :json

    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)
  end

  test "spends the server nonce, so a replay is refused" do
    registration = signed_registration(@challenge)
    post activation_registrations_path, params: registration, as: :json
    post activation_registrations_path, params: registration, as: :json

    assert_response :unprocessable_content
    assert_equal({ "error" => "expired" }, response.parsed_body)
  end

  test "refuses a registration the unit's key did not sign" do
    post activation_registrations_path, params: signed_registration(@challenge).merge(version: "9.9.9+1"), as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "bad_signature" }, response.parsed_body)

    post activation_registrations_path, params: signed_registration(@challenge).merge(release: false), as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "bad_signature" }, response.parsed_body)
    assert_not Unit.exists?(device_id: vectors["device_id"])
  end

  test "refuses another key for a known unit, and keeps it for an admin to look at" do
    post activation_registrations_path, params: signed_registration(@challenge, device_id: units(:bound).device_id), as: :json

    assert_response :conflict
    assert_equal({ "error" => "other_key" }, response.parsed_body)
    assert units(:bound).keys.find_by!(public_key: vectors["unit_public_key"]).pending?
  end

  # A server nonce travels in clear over Bluetooth, so whoever hears it must not be able to spend it.
  test "a registration the unit did not sign leaves the server nonce for the one it did" do
    post activation_registrations_path, params: signed_registration(@challenge).merge(signature: "00" * 64), as: :json
    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    assert_response :created
  end

  test "refuses a revoked key, which stays revoked" do
    unit = Unit.create!(device_id: vectors["device_id"], serial: 9)
    key = unit.keys.create!(public_key: vectors["unit_public_key"], status: :revoked)

    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    assert_response :conflict
    assert_equal({ "error" => "revoked_key" }, response.parsed_body)
    assert key.reload.revoked?
  end

  test "refuses a malformed registration" do
    post activation_registrations_path, params: signed_registration(@challenge).merge(device_id: "XYZ"), as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)

    post activation_registrations_path, params: { nothing: true }, as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)
  end

  test "refuses a new unit past the daily cap" do
    Unit.insert_all(Array.new(Unit::NEW_PER_DAY) { |index| { device_id: format("%016x", 0xa000 + index), serial: 100 + index } })

    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    assert_response :service_unavailable
    assert_equal({ "error" => "daily_cap" }, response.parsed_body)
  end
end
