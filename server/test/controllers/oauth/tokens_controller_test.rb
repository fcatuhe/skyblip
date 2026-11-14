require "test_helper"

class OAuth::TokensControllerTest < ActionDispatch::IntegrationTest
  include ConnectingAgents

  test "a code redeems once for an access token and a refresh token, never cached" do
    tokens = connect(client_id: CLAUDE_CODE)

    assert_response :success
    assert_equal "no-store", response.headers["Cache-Control"]
    assert_equal "Bearer", tokens["token_type"]
    assert_equal OAuth::Grant::ACCESS_LIFETIME.to_i, tokens["expires_in"]
    assert tokens["refresh_token"].present?
  end

  # Whoever reads the production log must not find a code and its verifier side by side.
  test "a token exchange logs neither the code nor the verifier" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")

    log = logged_by_controllers { redeem code, client_id: CLAUDE_CODE }

    assert_response :success
    assert_includes log, %("code" => "[FILTERED]")
    assert_not_includes log, code
    assert_not_includes log, code_verifier
  end

  # RFC 6749 3.2: parameters in the query string would land in the access logs of Caddy and kamal-proxy.
  test "a token request with its parameters in the query string is refused, and the code stays unspent" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")

    post oauth_token_path(grant_type: "authorization_code", code:, client_id: CLAUDE_CODE, redirect_uri: LOOPBACK,
                          code_verifier:)
    assert_response :bad_request

    assert redeem(code, client_id: CLAUDE_CODE).key?("access_token")
  end

  test "a code_verifier that does not match the challenge is refused" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")

    redeem code, client_id: CLAUDE_CODE, verifier: SecureRandom.urlsafe_base64(48)

    assert_response :bad_request
    assert_equal "invalid_grant", response.parsed_body["error"]
  end

  # OAuth 2.1 4.1.3: a code presented twice was stolen, so the connection it gave dies, as on a reused refresh token.
  test "a code redeemed twice is refused, and the connection it gave is revoked" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")
    first = redeem(code, client_id: CLAUDE_CODE)

    redeem code, client_id: CLAUDE_CODE

    assert_equal "invalid_grant", response.parsed_body["error"]
    mcp "tools/list", token: first.fetch("access_token")
    assert_response :unauthorized
    assert_empty users(:francois).oauth_grants
  end

  # A spent code sits in browser history, so holding one without the verifier must not cut the person's agent off.
  test "a spent code presented without its verifier is refused, and the tokens it gave keep working" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")
    first = redeem(code, client_id: CLAUDE_CODE)

    redeem code, client_id: CLAUDE_CODE, verifier: SecureRandom.urlsafe_base64(48)

    assert_equal "invalid_grant", response.parsed_body["error"]
    mcp "tools/list", token: first.fetch("access_token")
    assert_response :success
  end

  test "a code redeemed by another client is refused" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")

    redeem code, client_id: CODEX

    assert_equal "invalid_grant", response.parsed_body["error"]
  end

  test "a code redeemed after its minute is refused" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")

    travel OAuth::Code::LIFETIME + 1.second do
      redeem code, client_id: CLAUDE_CODE
    end

    assert_equal "invalid_grant", response.parsed_body["error"]
  end

  test "a refresh token rotates: it gives new tokens once" do
    tokens = connect(client_id: CLAUDE_CODE)

    post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                     client_id: CLAUDE_CODE }

    assert_response :success
    assert_not_equal tokens.fetch("refresh_token"), response.parsed_body.fetch("refresh_token")
  end

  # Rotation is how a stolen refresh token shows: the thief and the agent cannot both use it.
  test "a refresh token used twice revokes the connection" do
    tokens = connect(client_id: CLAUDE_CODE)
    post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                     client_id: CLAUDE_CODE }

    assert_event_reported "oauth_token.refused", payload: { error: "invalid_grant", grant_type: "refresh_token" } do
      post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                       client_id: CLAUDE_CODE }
    end

    assert_equal "invalid_grant", response.parsed_body["error"]
    assert_empty users(:francois).oauth_grants
  end

  # An admin's approval is who may read: an agent must not keep reading by refreshing forever.
  test "refreshing never stretches a connection past its approval, the admin approves again" do
    tokens = connect(client_id: CLAUDE_CODE)

    4.times do
      travel 7.days
      post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                       client_id: CLAUDE_CODE }
      tokens = response.parsed_body
      assert_response :success
    end

    travel_to OAuth::Grant.sole.approved_at + OAuth::Grant::APPROVAL_LIFETIME + 1.minute
    post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                     client_id: CLAUDE_CODE }
    assert_equal "invalid_grant", response.parsed_body["error"]
    mcp "tools/list", token: tokens.fetch("access_token")
    assert_response :unauthorized
  end

  test "an access token never outlives the approval either, and says so in expires_in" do
    tokens = connect(client_id: CLAUDE_CODE)
    approval_ends = OAuth::Grant.sole.approved_at + OAuth::Grant::APPROVAL_LIFETIME

    travel_to approval_ends - 1.minute
    post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                     client_id: CLAUDE_CODE }
    assert_operator response.parsed_body.fetch("expires_in"), :<=, 60

    travel 2.minutes
    mcp "tools/list", token: response.parsed_body.fetch("access_token")
    assert_response :unauthorized
  end

  test "approving again renews the connection for another approval lifetime" do
    connect(client_id: CLAUDE_CODE)

    travel OAuth::Grant::APPROVAL_LIFETIME - 1.day
    tokens = connect(client_id: CLAUDE_CODE)
    travel 2.days

    post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                     client_id: CLAUDE_CODE }
    assert_response :success
  end

  test "a token for another resource is refused as invalid_target" do
    code = approve(client_id: CLAUDE_CODE).fetch("code")

    post oauth_token_path, params: { grant_type: "authorization_code", code:, client_id: CLAUDE_CODE,
                                     redirect_uri: LOOPBACK, code_verifier:, resource: "https://elsewhere.example/mcp" }

    assert_equal "invalid_target", response.parsed_body["error"]
  end

  # Taking a client off the list must cut the agents already connected, not leave them reading until they lapse.
  test "a client taken off the list loses its connection, at /mcp and at the token endpoint" do
    tokens = connect(client_id: CLAUDE_CODE)

    with_config oauth_client_ids: Rails.configuration.x.oauth_client_ids - [ CLAUDE_CODE ] do
      mcp "tools/list", token: tokens.fetch("access_token")
      assert_response :unauthorized

      post oauth_token_path, params: { grant_type: "refresh_token", refresh_token: tokens.fetch("refresh_token"),
                                       client_id: CLAUDE_CODE }
      assert_equal "invalid_grant", response.parsed_body["error"]
    end
  end

  test "a grant type skyBlip does not take is refused" do
    assert_event_reported "oauth_token.refused", payload: { error: "unsupported_grant_type", grant_type: "other" } do
      post oauth_token_path, params: { grant_type: "client_credentials", client_id: CLAUDE_CODE }
    end

    assert_response :bad_request
    assert_equal "unsupported_grant_type", response.parsed_body["error"]
  end
end
