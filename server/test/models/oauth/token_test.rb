require "test_helper"

class OAuth::TokenTest < ActiveSupport::TestCase
  # A code reuse or a Disconnect can revoke the grant while a refresh of it is under way.
  test "a refresh token whose grant was revoked after it was loaded is refused as an OAuth error" do
    grant = OAuth::Grant.approve(user: users(:francois), client: OAuth::Client.create!(
      document_url: ConnectingAgents::CLAUDE_CODE, name: "Claude Code", redirect_uris: [ ConnectingAgents::LOOPBACK ], fetched_at: Time.current))
    loaded = OAuth::Token.refresh.find_by!(digest: OAuth.digest(grant.issue_tokens[:refresh_token]))

    grant.revoke

    error = assert_raises(OAuth::Error) { loaded.rotate }
    assert_equal "invalid_grant", error.code
  end
end
