require "test_helper"

class McpRegistryProofsControllerTest < ActionDispatch::IntegrationTest
  # The MCP Registry reads this record to let skyblip.eu publish servers named eu.skyblip/*.
  test "the proof record names the public key, in plain text" do
    public_key = [ OpenSSL::PKey.new_raw_private_key("ED25519", "\x01" * 32).raw_public_key ].pack("m0")

    with_config mcp_registry_public_key: public_key do
      get "/.well-known/mcp-registry-auth"
    end

    assert_response :success
    assert_equal "text/plain", response.media_type
    assert_equal "v=MCPv1; k=ed25519; p=#{public_key}", response.body
  end

  test "with no key, there is no proof to read" do
    get "/.well-known/mcp-registry-auth"

    assert_response :not_found
  end
end
