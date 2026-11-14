require "test_helper"

class McpTest < ActionDispatch::IntegrationTest
  include ConnectingAgents

  # An agent holding no token learns from the 401 alone where skyBlip's authorization server is.
  test "an agent with no token is pointed at the resource metadata, and from there at the authorization server" do
    mcp "tools/list", token: nil

    assert_response :unauthorized
    challenge = response.headers["WWW-Authenticate"]
    assert_includes challenge, %(resource_metadata="https://www.example.com/.well-known/oauth-protected-resource/mcp")

    get URI(challenge[/resource_metadata="([^"]+)"/, 1]).path
    assert_equal "https://www.example.com/mcp", response.parsed_body["resource"]
    assert_equal [ "https://www.example.com" ], response.parsed_body["authorization_servers"]

    get authorization_server_path
    assert_equal true, response.parsed_body["client_id_metadata_document_supported"]
    assert_equal [ "S256" ], response.parsed_body["code_challenge_methods_supported"]
  end

  test "Claude Code, Codex, pi and Claude on the web each connect through their metadata document, to read-only tools" do
    [ [ CLAUDE_CODE, LOOPBACK ], [ CODEX, LOOPBACK ], [ PI, LOOPBACK ], [ CLAUDE, HOSTED ] ].each do |client_id, redirect_uri|
      mcp "tools/list", token: connect(client_id:, redirect_uri:).fetch("access_token")

      tools = response.parsed_body.dig("result", "tools")
      assert_equal %w[get_insights list_events list_errors get_unit], tools.pluck("name"), client_id
      assert tools.all? { it.dig("annotations", "readOnlyHint") }
    end
    assert_equal "tools/list", Event.where(name: "mcp.answered").last.payload["method"]
  end

  # A pilot writes in with the code from the app's settings, and the agent reads that browser's story in order.
  test "list_events reads a pilot's journal by support code or device, and logs the read" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    Event.create!(name: "device.noticed", payload: { key: "link_lost" }, context: { device: "go-00012a", browser: "01AZ018Z" }, occurred_at: 2.minutes.ago)
    Event.create!(name: "device.connected", context: { device: "go-00012a", browser: "01AZ018Z" }, occurred_at: 1.minute.ago)
    Event.create!(name: "device.noticed", context: { device: "bravo-3f2a" }, occurred_at: 1.minute.ago)

    lines = call_tool("list_events", { support_code: "01az-018z", name: "device." }, token:).lines.map { JSON.parse(it) }
    assert_equal %w[device.noticed device.connected], lines.pluck("name")
    assert_equal "link_lost", lines.first.dig("payload", "key")

    catalog = call_tool("list_events", { device: "go-00012a" }, token:).lines.map { JSON.parse(it) }
    assert_equal Event::Catalog.names.size, catalog.count { it["event"] }
    assert_equal({ "by_id" => users(:francois).id, "device" => "go-00012a", "limit" => Event::LIMIT },
      Event.where(name: "event.listed").last.payload)
  end

  test "list_errors groups what broke, and get_unit reads a unit's record" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    Rails.error.report RuntimeError.new("the challenge expired"), handled: true, source: "activation"

    problems = JSON.parse(call_tool("list_errors", {}, token:))
    assert_equal [ [ "RuntimeError", "the challenge expired", 1, true ] ], problems.map { it.values_at("error", "message", "count", "new") }

    unit = JSON.parse(call_tool("get_unit", { device: units(:bound).slug }, token:))
    assert_equal [ 1, "000001" ], unit.values_at("serial", "address")
    assert_equal [ "active" ], unit["keys"].pluck("status")
    assert_equal [ [ "0.3.0+1300", true ] ], unit["signed_firmware"].map { it.values_at("version", "release") }
    assert_equal [], unit["reported_firmware"]

    call_tool("get_unit", { device: "go-00ffff" }, token:)
    assert response.parsed_body.dig("result", "isError")
  end

  test "get_insights reads the numbers for the period and the one before" do
    insights = JSON.parse(call_tool("get_insights", { days: 30 }, token: connect(client_id: CLAUDE_CODE).fetch("access_token")))

    assert_equal %w[now before], insights.keys
    assert_includes insights["now"].keys, "stranded"
  end

  # An admin demoted after connecting an agent reads nothing more through it.
  test "an agent whose admin was demoted reads nothing" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    users(:francois).update! role: :user

    call_tool("list_events", {}, token:)

    assert response.parsed_body.dig("result", "isError")
    assert_not Event.exists?(name: "event.listed")
  end
end
