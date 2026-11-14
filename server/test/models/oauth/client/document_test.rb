require "test_helper"

class OAuth::Client::DocumentTest < ActiveSupport::TestCase
  URL = "https://pi.dev/oauth/client.json"

  test "a document keeps the loopback and allowlisted redirects, and drops the others" do
    document = OAuth::Client::Document.new(URL, metadata(redirect_uris: [
      "http://127.0.0.1/callback", "https://claude.ai/api/mcp/auth_callback", "https://evil.example/cb", "app://cb"
    ]))

    assert_equal %w[http://127.0.0.1/callback https://claude.ai/api/mcp/auth_callback], document.redirect_uris
    assert_equal "pi", document.name
  end

  test "a document is refused when it names another client, holds a secret, has no name or no usable redirect" do
    [ metadata(client_id: "https://pi.dev/oauth/other.json"), metadata(token_endpoint_auth_method: "client_secret_basic"),
      metadata(token_endpoint_auth_method: nil), metadata(client_name: ""), metadata(redirect_uris: %w[https://evil.example/cb]),
      [ "not", "an", "object" ] ].each do |refused|
      assert_raises(OAuth::Error, refused.inspect) { OAuth::Client::Document.new(URL, refused) }
    end
  end

  # ChatGPT on the web signs its token requests with a key, and skyBlip takes public clients alone.
  test "ChatGPT's own web document is refused, for it is not a public client" do
    error = assert_raises(OAuth::Error) do
      vcr("oauth/chatgpt.com") { OAuth::Client::Document.fetch("https://chatgpt.com/oauth/client.json") }
    end

    assert_match "is not a public client", error.message
  end

  private
    def metadata(**overrides)
      { "client_id" => URL, "client_name" => "pi", "redirect_uris" => %w[http://127.0.0.1/callback],
        "token_endpoint_auth_method" => "none" }.merge(overrides.stringify_keys)
    end
end
