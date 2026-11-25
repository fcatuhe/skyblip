require "test_helper"
require_relative "../../test_helpers/analytics_test_helper"

class Admin::McpTest < ActionDispatch::IntegrationTest
  include ConnectingAgents
  include AnalyticsTestHelper

  # An agent holding no token learns from the 401 alone where skyBlip's authorization server is.
  test "an agent with no token is pointed at the resource metadata, and from there at the authorization server" do
    mcp "tools/list", token: nil

    assert_response :unauthorized
    challenge = response.headers["WWW-Authenticate"]
    assert_includes challenge, %(resource_metadata="https://www.example.com/.well-known/oauth-protected-resource/admin/mcp")

    get URI(challenge[/resource_metadata="([^"]+)"/, 1]).path
    assert_equal "https://www.example.com/admin/mcp", response.parsed_body["resource"]
    assert_equal [ "https://www.example.com" ], response.parsed_body["authorization_servers"]

    get authorization_server_path
    assert_equal true, response.parsed_body["client_id_metadata_document_supported"]
    assert_equal [ "S256" ], response.parsed_body["code_challenge_methods_supported"]
  end

  # The admin server answered at /mcp until 25 November 2026, and a token granted for that address opens nothing here.
  test "a token granted for the old /mcp address is refused" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    OAuth::Grant.last.update!(resource: "https://www.example.com/mcp")

    mcp "tools/list", token: token

    assert_response :unauthorized
  end

  test "Claude Code, Codex, pi and Claude on the web each connect through their metadata document, to tools that read but one" do
    [ [ CLAUDE_CODE, LOOPBACK ], [ CODEX, LOOPBACK ], [ PI, LOOPBACK ], [ CLAUDE, HOSTED ] ].each do |client_id, redirect_uri|
      mcp "tools/list", token: connect(client_id:, redirect_uri:).fetch("access_token")

      tools = response.parsed_body.dig("result", "tools")
      assert_equal %w[get_insights list_events list_errors resolve_error get_unit get_analytics list_visitors get_visitor list_outbound_clicks],
        tools.pluck("name"), client_id
      assert_equal %w[resolve_error], tools.reject { it.dig("annotations", "readOnlyHint") }.pluck("name")
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

    errors = JSON.parse(call_tool("list_errors", {}, token:))
    assert_equal [ [ "RuntimeError", "the challenge expired", 1, true ] ], errors.map { it.values_at("error", "message", "count", "new") }

    unit = JSON.parse(call_tool("get_unit", { device: units(:bound).slug }, token:)).sole
    assert_equal [ 1, "000001" ], unit.values_at("serial", "address")
    assert_equal [ "active" ], unit["keys"].pluck("status")
    assert_equal [ [ "0.3.0+1300", true, "plus", "attested" ] ], unit["firmware"].map { it.values_at("version", "release", "variant", "source") }

    bravo = JSON.parse(call_tool("get_unit", { device: "bravo-3f2a" }, token:)).sole
    assert_equal [ "bravo", nil, [ "read" ] ], [ bravo["product"], bravo["serial"], bravo["firmware"].pluck("source") ]

    call_tool("get_unit", { device: "go-00ffff" }, token:)
    assert response.parsed_body.dig("result", "isError")
  end

  # An agent resolves an error once its fix is deployed, and list_errors stops showing it until it happens again.
  test "resolve_error takes an error off list_errors, and logs who resolved it" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    Rails.error.report RuntimeError.new("the challenge expired"), handled: true
    fingerprint = JSON.parse(call_tool("list_errors", {}, token:)).sole["fingerprint"]

    resolved = JSON.parse(call_tool("resolve_error", { fingerprint: }, token:))

    assert resolved["resolved_at"]
    assert_equal [], JSON.parse(call_tool("list_errors", {}, token:))
    assert_equal({ "fingerprint" => fingerprint, "by_id" => users(:francois).id }, Event.find_by(name: "error.resolved").payload)

    call_tool("resolve_error", { fingerprint: "ffffffffffffffff" }, token:)
    assert response.parsed_body.dig("result", "isError")
  end

  test "get_insights reads the numbers for the period and the one before" do
    insights = JSON.parse(call_tool("get_insights", { days: 30 }, token: connect(client_id: CLAUDE_CODE).fetch("access_token")))

    assert_equal %w[now before], insights.keys
    assert_includes insights["now"].keys, "stranded"
  end

  # The owner asks whether anyone clicked to buy a Go, and the agent reads the affiliate links and then each click.
  test "list_outbound_clicks counts the clicks on each link out, lists each one, and logs the read" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    clear_analytics
    pilot = browse [ [ "/buy", 30 ] ], visitor: "pilot", started_at: 1.day.ago
    click pilot, "https://link.amazon/B00EaKI0W", text: "€68", section: "go"
    click pilot, "https://github.com/fcatuhe/skyblip", page: "/about", after: 5.seconds
    click browse([ [ "/buy", 30 ] ], visitor: "francois", started_at: 1.day.ago, user: users(:francois)), "https://link.amazon/B00EaKI0W"

    links = JSON.parse(call_tool("list_outbound_clicks", { href: "amazon" }, token:))
    assert_equal [ [ "https://link.amazon/B00EaKI0W", "go", "€68", 1, 1 ] ], links.map { it.values_at("href", "section", "text", "clicks", "visitors") }

    clicks = JSON.parse(call_tool("list_outbound_clicks", { each: true, segment: "all" }, token:))
    assert_equal [ [ "pilot", "/buy" ], [ "pilot", "/about" ], [ "francois", "/buy" ] ].sort, clicks.map { it.values_at("visitor_token", "page") }.sort
    assert_equal({ "by_id" => users(:francois).id, "tool" => "list_outbound_clicks", "segment" => "all", "each" => true },
      Event.where(name: "analytics.listed").last.payload)
  end

  # The agent reads the audience as the Analytics page shows it, narrows it to a page, then follows one visitor to a click.
  test "get_analytics, list_visitors and get_visitor read the overview, the visitors and one visitor's story" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    clear_analytics
    pilot = browse [ [ "/", 20 ], [ "/buy", 40 ] ], visitor: "pilot", started_at: 1.day.ago
    click pilot, "https://link.amazon/B00EaKI0W", text: "€68", section: "go", after: 30.seconds
    browse [ [ "/guides", 10 ] ], visitor: "reader", started_at: 2.days.ago
    browse [ [ "/admin/analytics", 10 ] ], visitor: "francois", started_at: 1.day.ago, user: users(:francois)

    overview = JSON.parse(call_tool("get_analytics", { days: 7 }, token:))
    assert_equal [ 2, 2, 3 ], overview["metrics"].values_at("visitors", "visits", "pageviews")
    assert_equal [ 1, 1 ], overview["series"].pluck("visits")
    assert_equal [ "/", "/buy", "/guides" ], overview.dig("breakdowns", "path").pluck("label").sort
    assert_equal [ "https://link.amazon/B00EaKI0W" ], overview["outbound_links"].pluck("href")

    visitors = JSON.parse(call_tool("list_visitors", { path: "/buy" }, token:))
    assert_equal [ [ "pilot", 2 ] ], visitors["visitors"].map { it.values_at("visitor_token", "views_count") }
    assert_equal false, visitors["more"]

    visitor = JSON.parse(call_tool("get_visitor", { visitor_token: "pilot" }, token:))
    assert_equal [ 1, 2 ], visitor.values_at("visits", "views")
    assert_equal %w[session_start $view $view $click $pagehide session_end], visitor["timeline"].pluck("type")
    assert_equal "to https://link.amazon/B00EaKI0W", visitor["timeline"].find { it["type"] == "$click" }["detail"]
    assert_equal({ "by_id" => users(:francois).id, "tool" => "get_visitor", "visitor_token" => "pilot", "source" => "browser", "page" => 1 },
      Event.where(name: "analytics.listed").last.payload)

    call_tool("get_visitor", { visitor_token: "nobody" }, token:)
    assert response.parsed_body.dig("result", "isError")
    call_tool("get_analytics", { start_on: "last week" }, token:)
    assert response.parsed_body.dig("result", "isError")
  end

  # The agent goes from the visitors who activated to a unit and the briefing journal that activated it, and back.
  test "an activation ties the analytics to the unit and to the support code of the briefing that asked" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    clear_analytics
    activate browse([ [ "/briefing/activate", 60 ] ], visitor: "pilot", started_at: 1.day.ago), units(:bound), browser: "5G3GRZ31"
    browse [ [ "/buy", 10 ] ], visitor: "reader", started_at: 1.day.ago

    assert_equal [ 2, 1 ], JSON.parse(call_tool("get_analytics", { days: 7 }, token:))["metrics"].values_at("visitors", "activated_visitors")
    assert_equal [ "pilot" ], JSON.parse(call_tool("list_visitors", { activation: "activated" }, token:))["visitors"].pluck("visitor_token")
    activations = JSON.parse(call_tool("get_visitor", { visitor_token: "pilot" }, token:))["activations"]
    assert_equal [ [ units(:bound).slug, "5G3GRZ31" ] ], activations.map { it.values_at("device", "support_code") }
    assert_equal [ "pilot" ], JSON.parse(call_tool("get_unit", { device: units(:bound).slug }, token:)).sole["activated_by"]
  end

  # An admin demoted after connecting an agent reads nothing more through it, even once promoted again.
  test "an agent whose admin was demoted is disconnected for good" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")
    users(:francois).update! role: :user
    users(:francois).update! role: :admin

    call_tool("list_events", {}, token:)

    assert_response :unauthorized
    assert_not Event.exists?(name: "event.listed")
  end
end
