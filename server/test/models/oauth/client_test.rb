require "test_helper"

class OAuth::ClientTest < ActiveSupport::TestCase
  # Each of these is a client_id an attacker could give to point skyBlip at an address, or a page, of their choosing.
  test "only a listed document is fetched, not another page on the same host" do
    [ "https://claude.ai/oauth/other-client-metadata", "https://chatgpt.com/oauth/client.json",
      "http://claude.ai/oauth/claude-code-client-metadata", "https://claude.ai/oauth/claude-code-client-metadata/",
      "https://claude.ai/oauth/claude-code-client-metadata?as=admin", "https://claude.ai.evil.example/client.json",
      "https://evil.example/client.json", "https://claude.ai:8443/oauth/claude-code-client-metadata",
      "https://user@claude.ai/oauth/claude-code-client-metadata", "https://claude.ai/oauth/../oauth/claude-code-client-metadata",
      "not a url", nil ].each do |document_url|
      error = assert_raises(OAuth::Error, document_url.inspect) { OAuth::Client.identified_by(document_url) }
      assert_equal "invalid_client", error.code
    end
  end

  test "a client read within the day is not fetched again, and one read longer ago is" do
    client = vcr("oauth/claude.ai") { OAuth::Client.identified_by(ConnectingAgents::CLAUDE_CODE) }
    assert_equal "Claude Code", client.name

    client.update! name: "Renamed since"
    assert_equal "Renamed since", OAuth::Client.identified_by(ConnectingAgents::CLAUDE_CODE).name

    travel OAuth::Client::FRESH_FOR + 1.minute do
      assert_equal "Claude Code", vcr("oauth/claude.ai") { OAuth::Client.identified_by(ConnectingAgents::CLAUDE_CODE) }.name
    end
  end

  # RFC 8252 7.3: the agent listens on whatever loopback port is free, so the port alone may differ.
  test "a loopback redirect matches on any port, and on nothing else" do
    client = native_client

    assert client.redirects_to?("http://127.0.0.1:53682/callback")
    assert client.redirects_to?("http://localhost/callback")
    assert_not client.redirects_to?("http://127.0.0.1:53682/elsewhere")
    assert_not client.redirects_to?("http://localhost:3000/callback?then=https://evil.example")
    assert_not client.redirects_to?("https://127.0.0.1/callback")
    assert_not client.redirects_to?("http://evil@127.0.0.1/callback")
    assert_not client.redirects_to?(nil)
  end

  test "a hosted redirect matches exactly" do
    client = native_client(redirect_uris: %w[https://claude.ai/api/mcp/auth_callback])

    assert client.redirects_to?("https://claude.ai/api/mcp/auth_callback")
    assert_not client.redirects_to?("https://claude.ai/api/mcp/auth_callback/")
    assert_not client.redirects_to?("https://claude.ai:444/api/mcp/auth_callback")
  end

  private
    def native_client(redirect_uris: %w[http://localhost/callback http://127.0.0.1/callback])
      OAuth::Client.create! document_url: ConnectingAgents::CLAUDE_CODE, name: "Claude Code",
                            redirect_uris:, fetched_at: Time.current
    end
end
