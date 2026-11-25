require "test_helper"

class McpTest < ActionDispatch::IntegrationTest
  # A connector reads the website with no account, so nothing it can call writes.
  test "an agent with no token lists the tools, which only read the website" do
    mcp "tools/list"

    assert_response :success
    tools = response.parsed_body.dig("result", "tools")
    assert_equal %w[list_pages search_pages get_page], tools.pluck("name")
    assert tools.all? { it.dig("annotations", "readOnlyHint") }
    assert_equal [ "tools/list", "/mcp" ], Event.where(name: "mcp.answered").last.then { [ it.payload["method"], it.context["path"] ] }
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
    call_tool("get_page", { url: "http://exa mple" })
    assert response.parsed_body.dig("result", "isError")
  end

  private
    def mcp(method, params = {})
      post mcp_path, params: { jsonrpc: "2.0", id: 1, method:, params: }.to_json,
        headers: { "Content-Type" => "application/json", "Accept" => "application/json, text/event-stream", "MCP-Protocol-Version" => "2025-06-18" }
    end

    def call_tool(name, arguments = {})
      mcp("tools/call", { name:, arguments: })
      response.parsed_body.dig("result", "content", 0, "text")
    end
end
