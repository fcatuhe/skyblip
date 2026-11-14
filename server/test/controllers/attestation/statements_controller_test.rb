require "test_helper"

class Attestation::StatementsControllerTest < ActionDispatch::IntegrationTest
  setup do
    @unit = Unit.create!(device_id: vectors["device_id"], serial: 9)
    @unit.keys.create!(public_key: vectors["unit_public_key"], status: :active)
    @challenge = Attestation::Challenge.create!
  end

  test "records the firmware the unit signed with its active key" do
    post attestation_statements_path, params: signed_statement(@challenge, release: false), as: :json

    assert_response :created
    assert_equal({ "version" => vectors["version"], "release" => false }, response.parsed_body)
    assert_equal [ [ vectors["version"], false ] ], @unit.firmwares.pluck(:version, :release)
  end

  test "the same firmware again keeps one row and moves its last signing, and another firmware opens a row" do
    post attestation_statements_path, params: signed_statement(@challenge), as: :json
    travel 1.day do
      post attestation_statements_path, params: signed_statement(Attestation::Challenge.create!), as: :json
    end
    assert_equal 1, @unit.firmwares.count
    assert_operator @unit.firmwares.sole.updated_at, :>, @unit.firmwares.sole.created_at + 23.hours

    post attestation_statements_path, params: signed_statement(Attestation::Challenge.create!, version: "0.4.0+1400"), as: :json
    assert_equal [ "0.4.0+1400", vectors["version"] ], @unit.firmwares.newest_first.pluck(:version)
  end

  test "spends the server nonce, so a replay is refused" do
    statement = signed_statement(@challenge)
    post attestation_statements_path, params: statement, as: :json
    post attestation_statements_path, params: statement, as: :json

    assert_response :unprocessable_content
    assert_equal({ "error" => "expired" }, response.parsed_body)
  end

  # A server nonce travels in clear over Bluetooth, so whoever hears it must not be able to spend it.
  test "refuses a statement the active key did not sign, and leaves the nonce for the one it did" do
    post attestation_statements_path, params: signed_statement(@challenge).merge(version: "9.9.9+1"), as: :json
    assert_response :unprocessable_content
    assert_equal({ "error" => "bad_signature" }, response.parsed_body)

    post attestation_statements_path, params: signed_statement(@challenge, unit_key: unit_key("11" * 32)), as: :json
    assert_equal({ "error" => "bad_signature" }, response.parsed_body)
    assert_empty @unit.firmwares

    post attestation_statements_path, params: signed_statement(@challenge), as: :json
    assert_response :created
  end

  test "refuses a unit the server does not know, and one with no active key" do
    post attestation_statements_path, params: signed_statement(@challenge, device_id: "1111111111111111"), as: :json
    assert_response :not_found
    assert_equal({ "error" => "unknown_unit" }, response.parsed_body)

    @unit.active_key.revoke
    post attestation_statements_path, params: signed_statement(@challenge), as: :json
    assert_response :conflict
    assert_equal({ "error" => "no_active_key" }, response.parsed_body)
  end

  test "refuses a malformed statement, a release that is not a JSON boolean included" do
    [ { device_id: "XYZ" }, { release: "true" }, { release: 1 }, { version: "" } ].each do |change|
      post attestation_statements_path, params: signed_statement(@challenge).merge(change), as: :json

      assert_response :unprocessable_content
      assert_equal({ "error" => "invalid" }, response.parsed_body)
    end

    post attestation_statements_path, params: "{not json", headers: { "Content-Type" => "application/json", "Accept" => "application/json" }
    assert_equal({ "error" => "invalid" }, response.parsed_body)
  end

  test "a recorded firmware is an event with its device, and a refusal is one with its reason" do
    assert_event_reported "unit.firmware_attested", payload: { device: "go-000009", version: vectors["version"], release: true, changed: true } do
      post attestation_statements_path, params: signed_statement(@challenge), as: :json
    end

    assert_event_reported "attestation.refused", payload: { reason: :expired, step: "statement" } do
      post attestation_statements_path, params: signed_statement(@challenge), as: :json
    end
  end
end
