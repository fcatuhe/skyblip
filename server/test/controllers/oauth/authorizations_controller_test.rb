require "test_helper"

class OAuth::AuthorizationsControllerTest < ActionDispatch::IntegrationTest
  include ConnectingAgents

  # An admin already signed in sees one question and no login.
  test "the consent names the agent, the admin it reads as, and where it sends the code" do
    sign_in_as users(:francois)
    vcr("oauth/claude.ai") { get new_oauth_authorization_path(consent_query(client_id: CLAUDE_CODE)) }

    assert_response :success
    assert_select "h1", text: "Connect Claude Code"
    assert_select "strong", text: "francois@skyblip.test"
    assert_select ".admin-details dd", text: "this computer"
    assert_select "p", text: /send them to the AI provider it uses/
    assert_select "form[action=?] button", oauth_authorization_path, text: "Connect Claude Code"
  end

  test "a visitor signs in first, and comes back to the consent" do
    get new_oauth_authorization_path(consent_query(client_id: CLAUDE_CODE))

    assert_redirected_to new_session_path
  end

  # Everybody's events are for admins, so a user may not connect an agent to read them.
  test "a user who is not an admin is refused, and nothing is granted" do
    sign_in_as users(:amelie)
    post oauth_authorization_path, params: consent_query(client_id: CLAUDE_CODE)

    assert_response :forbidden
    assert_empty OAuth::Grant.all
  end

  test "approving sends the code back to the agent with the state and skyBlip as the issuer" do
    answer = approve(client_id: CLAUDE_CODE)

    assert response.location.start_with?("http://127.0.0.1:53682/callback?")
    assert answer["code"].present?
    assert_equal "st4te", answer["state"]
    assert_equal "https://www.example.com", answer["iss"]
    assert_equal [ users(:francois) ], OAuth::Grant.all.map(&:user)
  end

  test "approving logs the redirect without the code" do
    log = logged_by_controllers { approve(client_id: CLAUDE_CODE) }

    assert_includes log, "code=[FILTERED]"
    assert_not_includes log, Rack::Utils.parse_query(URI(response.location).query).fetch("code")
  end

  test "approving the same agent twice keeps one connection" do
    2.times { approve(client_id: CLAUDE_CODE) }

    assert_equal 1, users(:francois).oauth_grants.count
  end

  test "cancelling tells the agent the person declined, and grants nothing" do
    sign_in_as users(:francois)
    vcr("oauth/claude.ai") do
      delete oauth_authorization_path, params: consent_query(client_id: CLAUDE_CODE)
    end

    assert_equal "access_denied", Rack::Utils.parse_query(URI(response.location).query)["error"]
    assert_empty OAuth::Grant.all
  end

  # skyBlip fetches only listed documents, so an unknown client_id is refused before any request leaves.
  test "a client off the list is refused on skyBlip's page, and nothing is fetched" do
    sign_in_as users(:francois)
    [ "https://agents.example.com/client.json", "https://claude.ai/share/any-page-posing-as-claude-code" ].each do |client_id|
      get new_oauth_authorization_path(consent_query(client_id:))

      assert_response :bad_request
      assert_select "p", text: /skyBlip does not know the client/
    end
    assert_empty OAuth::Client.all
  end

  # Claude on the web redeems the code on claude.ai's servers, so the person sees that the code leaves their computer.
  test "the consent for Claude on the web says the code returns to claude.ai" do
    sign_in_as users(:francois)
    vcr("oauth/claude.ai") do
      get new_oauth_authorization_path(consent_query(client_id: CLAUDE, redirect_uri: HOSTED))
    end

    assert_select "h1", text: "Connect Claude"
    assert_select ".admin-details dd", text: "claude.ai"
  end

  # A code sent to an address the client never registered is how a phished approval would leak.
  test "a redirect_uri the client did not register is refused on skyBlip's page, not followed" do
    sign_in_as users(:francois)
    vcr("oauth/claude.ai") do
      get new_oauth_authorization_path(consent_query(client_id: CLAUDE_CODE, redirect_uri: "https://evil.example/cb"))
    end

    assert_response :bad_request
    assert_select "p", text: /redirect_uri is not one Claude Code registered/
  end

  test "a request without PKCE goes back to the agent as invalid_request" do
    sign_in_as users(:francois)
    vcr("oauth/claude.ai") do
      get new_oauth_authorization_path(consent_query(client_id: CLAUDE_CODE).except(:code_challenge))
    end

    answer = Rack::Utils.parse_query(URI(response.location).query)
    assert_equal "invalid_request", answer["error"]
    assert_equal "st4te", answer["state"]
  end

  test "a scope skyBlip does not grant goes back to the agent as invalid_scope" do
    sign_in_as users(:francois)
    vcr("oauth/claude.ai") do
      get new_oauth_authorization_path(consent_query(client_id: CLAUDE_CODE, scope: "read write"))
    end

    assert_equal "invalid_scope", Rack::Utils.parse_query(URI(response.location).query)["error"]
  end
end
