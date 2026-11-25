require "test_helper"
require "json_schemer"

class Mcps::ServerCardsControllerTest < ActionDispatch::IntegrationTest
  test "the server card is valid, and names the server as it introduces itself once connected" do
    get mcp_server_card_path

    assert_response :success
    assert_equal "application/mcp-server-card+json", response.media_type
    card = JSON.parse(response.body)
    assert_empty schema.validate(card).map { it["error"] }
    assert_equal [ "https://skyblip.eu/mcp", "streamable-http" ], card["remotes"].sole.values_at("url", "type")

    post mcp_path, params: { jsonrpc: "2.0", id: 1, method: "initialize",
                             params: { protocolVersion: "2025-06-18", capabilities: {}, clientInfo: { name: "test", version: "1" } } }.to_json,
      headers: { "Content-Type" => "application/json", "Accept" => "application/json, text/event-stream" }
    assert_equal card.slice("name", "version", "title"), response.parsed_body.dig("result", "serverInfo").slice("name", "version", "title")
  end

  test "a browser may read the card, and a client that holds it is told it has not changed" do
    get mcp_server_card_path

    assert_equal "*", response.headers["Access-Control-Allow-Origin"]
    assert_equal "max-age=3600, public", response.headers["Cache-Control"]
    etag = response.headers["ETag"]

    get mcp_server_card_path, headers: { "If-None-Match" => etag }
    assert_response :not_modified
  end

  private
    # The schema comes from modelcontextprotocol/experimental-ext-server-card at 28db12c9, so the test reads no network.
    def schema = JSONSchemer.schema(file_fixture("mcp/server-card.schema.json").read).ref("#/$defs/ServerCard")
end
