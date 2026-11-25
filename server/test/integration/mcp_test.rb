require "test_helper"

class McpTest < ActionDispatch::IntegrationTest
  include ConnectingAgents

  # A connector reads the website with no account, so nothing it can call writes.
  test "an agent with no token lists the tools, which only read the website" do
    mcp "tools/list"

    assert_response :success
    tools = response.parsed_body.dig("result", "tools")
    assert_equal %w[list_pages search_pages get_page], tools.pluck("name")
    assert tools.all? { it.dig("annotations", "readOnlyHint") }
    assert_equal [ "tools/list", "/mcp" ], Event.where(name: "mcp.answered").last.then { [ it.payload["method"], it.context["path"] ] }
  end

  # The admin tools live at /admin/mcp alone, so an admin's token opens nothing more here.
  test "an admin's token at /mcp still reaches only the public tools" do
    token = connect(client_id: CLAUDE_CODE).fetch("access_token")

    mcp "tools/list", token: token
    assert_equal %w[list_pages search_pages get_page], response.parsed_body.dig("result", "tools").pluck("name")

    mcp "tools/call", { name: "get_insights", arguments: {} }, token: token
    assert_equal "Tool not found: get_insights", response.parsed_body.dig("error", "data")
  end

  # Agents still pointed at /mcp after the admin tools moved called list_errors here, which is their mistake, not the server's.
  test "a tool this server does not have is refused to the agent, and logs no error" do
    mcp "tools/call", { name: "list_errors", arguments: {} }

    assert_equal "Tool not found: list_errors", response.parsed_body.dig("error", "data")
    assert_equal "invalid_params", Event.where(name: "mcp.answered").last.payload["error"]
    assert_not Event.exists?(name: "error.reported")
  end

  test "an argument a tool does not take is refused to the agent by name, and logs no error" do
    call_tool("list_pages", { language: "fr", days: 7 })

    assert response.parsed_body.dig("result", "isError")
    assert_match(/days/, response.parsed_body.dig("result", "content", 0, "text"))
    assert_not Event.exists?(name: "error.reported")
  end

  test "list_pages lists every page in a language, and in English the pages not translated yet" do
    pages = JSON.parse(call_tool("list_pages", { language: "fr" }))

    assert_equal Content::Page.read_in(:fr).size, pages.size
    about = pages.find { it["url"] == "https://skyblip.eu/fr/a-propos" }
    assert_equal [ "fr", "https://skyblip.eu/fr/a-propos.md" ], about.values_at("language", "markdown_url")
    assert_equal "en", pages.find { it["url"] == "https://skyblip.eu/ads-l/nmea" }["language"]
  end

  # A pilot asks the agent how to see Go's traffic in SkyDemon, and the agent finds the page and reads it.
  test "search_pages finds the page on a question, and get_page reads it whole" do
    found = JSON.parse(call_tool("search_pages", { query: "SkyDemon Bluetooth" })).first
    assert_equal "https://skyblip.eu/phone", found["url"]
    assert_match(/SkyDemon/, found["excerpt"])

    page = call_tool("get_page", { url: found["url"] })
    assert page.start_with?("Source: https://skyblip.eu/phone\n\n")
    assert_includes page, "\n# "
  end

  test "get_page reads a page at its path or its Markdown's, and refuses an address with no page" do
    assert call_tool("get_page", { url: "/fr/a-propos.md" }).start_with?("Source: https://skyblip.eu/fr/a-propos\n")
    assert call_tool("get_page", { url: "https://skyblip.eu/" }).start_with?("Source: https://skyblip.eu/\n")

    call_tool("get_page", { url: "/nope" })
    assert response.parsed_body.dig("result", "isError")
    [ "http://exa mple", "mailto:x", "urn:isbn:1" ].each do |url|
      call_tool("get_page", { url: })
      assert response.parsed_body.dig("result", "isError"), url
    end
    assert_not Event.exists?(name: "error.reported")
  end

  private
    def mcp(method, params = {}, token: nil)
      post mcp_path, params: { jsonrpc: "2.0", id: 1, method:, params: }.to_json,
        headers: { "Content-Type" => "application/json", "Accept" => "application/json, text/event-stream",
                   "MCP-Protocol-Version" => "2025-06-18", **({ "Authorization" => "Bearer #{token}" } if token) }
    end

    def call_tool(name, arguments = {})
      mcp("tools/call", { name:, arguments: })
      response.parsed_body.dig("result", "content", 0, "text")
    end
end
