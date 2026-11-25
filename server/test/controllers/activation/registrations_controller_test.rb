require "test_helper"

class Activation::RegistrationsControllerTest < ActionDispatch::IntegrationTest
  setup do
    @challenge = Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key_id: "890026b1")
  end

  test "binds a new unit and answers its serial with an unlock code for this unit and this session" do
    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    assert_response :created
    serial, signature = response.parsed_body.values_at("serial", "signature")
    assert_equal Unit.find_by!(device_id: vectors["device_id"]).serial, serial
    message = Identity::Message.unlock(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], serial:, unit_nonce: vectors["unit_nonce"])
    assert server_verifies?(signature, message)
  end

  test "records the unit's firmware and whether it is a release, and not the address it came from" do
    post activation_registrations_path, params: signed_registration(@challenge, release: false), as: :json, headers: { "REMOTE_ADDR" => "203.0.113.8" }

    unit = Unit.find_by!(device_id: vectors["device_id"])
    assert_equal [ vectors["version"], "890026b1", false ], [ unit.active_key.version, unit.active_key.server_key_id, unit.active_key.release ]
    assert_not unit.has_attribute?(:enrolled_from)
    assert_not unit.active_key.has_attribute?(:ip_address)
  end

  # A unit activated without them waited for its next connection to be named, and its tag never came.
  test "records the product, the variant and the tag the unit signed, beside its firmware" do
    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    unit = Unit.find_by!(device_id: vectors["device_id"])
    assert_equal [ "go", "plus", 0x3F2A ], [ unit.product, unit.variant, unit.bluetooth_tag ]
    assert_equal [ [ vectors["version"], true, "plus", "attested" ] ], unit.firmwares.pluck(:version, :release, :variant, :source)
  end

  test "activates a unit on older firmware, whose registration of v1 names no product, variant nor tag" do
    post activation_registrations_path, params: signed_registration_v1(@challenge), as: :json

    assert_response :created
    unit = Unit.find_by!(device_id: vectors["device_id"])
    assert unit.activated?
    assert_equal [ nil, nil, nil ], [ unit.product, unit.variant, unit.bluetooth_tag ]
    assert_equal [ [ vectors["version"], true, nil ] ], unit.firmwares.pluck(:version, :release, :variant)
  end

  test "refuses a product, a variant or a tag the unit did not sign, and one of v1 that names them" do
    [ { variant: "standard" }, { product: "bravo" }, { bluetooth_tag: 0x3F2B } ].each do |forged|
      post activation_registrations_path, params: signed_registration(@challenge).merge(forged), as: :json

      assert_response :unprocessable_content
      assert_equal({ "error" => "bad_signature" }, response.parsed_body)
    end

    post activation_registrations_path, params: signed_registration_v1(@challenge).merge(product: "go", variant: "plus", bluetooth_tag: 0x3F2A), as: :json
    assert_equal({ "error" => "bad_signature" }, response.parsed_body)
    assert_not Unit.exists?(device_id: vectors["device_id"])
  end

  test "records the firmware an active key signed, and none for a key it refuses" do
    post activation_registrations_path, params: signed_registration(@challenge, release: false), as: :json
    assert_equal [ [ vectors["version"], false ] ], Unit.find_by!(device_id: vectors["device_id"]).firmwares.pluck(:version, :release)

    assert_no_difference -> { units(:bound).firmwares.count } do
      post activation_registrations_path, params: signed_registration(Activation::Challenge.issue(unit_nonce: vectors["unit_nonce"], server_key_id: "890026b1"), device_id: units(:bound).device_id), as: :json
    end
    assert_response :conflict
  end

  test "refuses a registration that does not say whether it is a release" do
    post activation_registrations_path, params: signed_registration(@challenge).except(:release), as: :json

    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)
  end

  test "refuses a release that is not a JSON boolean, which a cast would have read as true" do
    [ "this is not a boolean", "true", 1 ].each do |release|
      post activation_registrations_path, params: signed_registration(@challenge).merge(release:), as: :json

      assert_response :unprocessable_content
      assert_equal({ "error" => "invalid" }, response.parsed_body)
    end
    assert_not Unit.exists?(device_id: vectors["device_id"])
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

  # The unit signs the key id it trusts, so a registration naming another key than its challenge did is refused before its signature.
  test "refuses a registration for another server key than its challenge, and leaves the nonce" do
    other_key = Activation::Challenge.new(unit_nonce: @challenge.unit_nonce, server_nonce: @challenge.server_nonce, server_key_id: "a9649d2d")
    post activation_registrations_path, params: signed_registration(other_key), as: :json

    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)

    post activation_registrations_path, params: signed_registration(@challenge), as: :json
    assert_response :created
  end

  # OpenSSL verifies this signature for any message: the identity point as key and as R, and S zero.
  test "refuses a small-order key, whose signature anybody can make without a private key" do
    forged = "01" + "00" * 31
    post activation_registrations_path, params: signed_registration(@challenge).merge(public_key: forged, signature: forged + "00" * 32), as: :json

    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)
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
    Unit.insert_all(Array.new(Unit.daily_cap) { |index| { device_id: format("%016x", 0xa000 + index), serial: 100 + index, activated_at: Time.current } })

    post activation_registrations_path, params: signed_registration(@challenge), as: :json

    assert_response :service_unavailable
    assert_equal({ "error" => "daily_cap" }, response.parsed_body)
  end

  test "answers a body that is not JSON as invalid, in JSON" do
    post activation_registrations_path, params: "{not json", headers: { "Content-Type" => "application/json", "Accept" => "application/json" }

    assert_response :unprocessable_content
    assert_equal({ "error" => "invalid" }, response.parsed_body)
  end

  test "refuses a new unit past the cap of the network it comes from" do
    with_memory_cache do
      Unit::NEW_PER_NETWORK_PER_DAY.times { |index| Unit.register(device_id: format("%016x", 0xa000 + index), public_key: "d4" * 32, ip_address: "203.0.113.8") }

      post activation_registrations_path, params: signed_registration(@challenge), as: :json, headers: { "REMOTE_ADDR" => "203.0.113.8" }
    end

    assert_response :too_many_requests
    assert_equal({ "error" => "network_cap" }, response.parsed_body)
  end

  # A pilot stuck at activation is answered from these, beside what the page showed them.
  test "a new unit is an event with its serial and device, and a refusal is one with its reason" do
    assert_event_reported "unit.enrolled", payload: { serial: 3, device: "go-000003" } do
      assert_event_reported "unit_key.bound", payload: { device: "go-000003", status: "active", renewed: false } do
        post activation_registrations_path, params: signed_registration(@challenge), as: :json
      end
    end

    assert_event_reported "activation.refused", payload: { reason: :expired, step: "registration" } do
      post activation_registrations_path, params: signed_registration(@challenge), as: :json
    end
  end

  # The analytics tie a visitor to the units it activated and to its briefing journal through this one event.
  test "an activation is tracked on the browser's visit with its unit and support code, and a refusal is not" do
    Ahoy.api_only = false
    briefing = { "X-Briefing-Browser" => "5G3GRZ31", "User-Agent" => "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Mobile Safari/537.36" }
    post activation_registrations_path, params: signed_registration(@challenge), as: :json, headers: briefing
    post activation_registrations_path, params: signed_registration(@challenge), as: :json, headers: briefing

    unit = Unit.find_by!(device_id: vectors["device_id"])
    activation = Ahoy::Event.find_sole_by(name: "unit_activated")
    assert_equal({ "unit_id" => unit.id, "device" => "go-000003", "browser" => "5G3GRZ31" }, activation.properties)
    assert_equal [ activation.visit ], Ahoy::Visit.activating(unit).to_a
  ensure
    Ahoy.api_only = true
  end
end
