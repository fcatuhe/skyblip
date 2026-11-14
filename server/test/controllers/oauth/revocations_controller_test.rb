require "test_helper"

class OAuth::RevocationsControllerTest < ActionDispatch::IntegrationTest
  include ConnectingAgents

  test "an agent signing out revokes its access token" do
    tokens = connect(client_id: CLAUDE_CODE)

    post oauth_revocation_path, params: { token: tokens.fetch("access_token"), client_id: CLAUDE_CODE }

    assert_response :success
    mcp "tools/list", token: tokens.fetch("access_token")
    assert_response :unauthorized
  end

  # RFC 7009 2.1: signing out with the refresh token ends the connection, its access tokens included.
  test "an agent revoking its refresh token loses its access token too, and leaves Settings" do
    tokens = connect(client_id: CLAUDE_CODE)

    post oauth_revocation_path, params: { token: tokens.fetch("refresh_token"), client_id: CLAUDE_CODE }

    assert_response :success
    mcp "tools/list", token: tokens.fetch("access_token")
    assert_response :unauthorized
    assert_empty users(:francois).oauth_grants
  end

  # RFC 7009 2.1: the token comes in the form body, never in a query string an access log keeps.
  test "a token sent in the query string is not revoked" do
    tokens = connect(client_id: CLAUDE_CODE)

    post oauth_revocation_path(token: tokens.fetch("access_token"), client_id: CLAUDE_CODE)

    mcp "tools/list", token: tokens.fetch("access_token")
    assert_response :success
  end

  # RFC 7009 2.1: only the client the token was issued to may revoke it.
  test "another client cannot revoke the token" do
    tokens = connect(client_id: CLAUDE_CODE)

    post oauth_revocation_path, params: { token: tokens.fetch("access_token"), client_id: CODEX }

    assert_response :success
    mcp "tools/list", token: tokens.fetch("access_token")
    assert_response :success
  end
end
