require "test_helper"

class AiCatalogsControllerTest < ActionDispatch::IntegrationTest
  # A client that knows only skyblip.eu finds the public MCP server from the well-known catalog.
  test "the AI Catalog points at the public MCP server's card, and the card at the server" do
    get "/.well-known/ai-catalog.json"

    assert_response :success
    assert_equal "application/ai-catalog+json", response.media_type
    catalog = JSON.parse(response.body)
    assert_equal "0.9", catalog["specVersion"]
    entry = catalog["entries"].sole
    assert_equal [ "urn:air:skyblip.eu:website:mcp", "application/mcp-server-card+json" ], entry.values_at("identifier", "type")

    get URI(entry["url"]).path
    assert_equal "https://skyblip.eu/mcp", JSON.parse(response.body)["remotes"].sole["url"]
  end

  test "another well-known address still answers empty" do
    get "/.well-known/nothing-here"

    assert_response :no_content
  end
end
