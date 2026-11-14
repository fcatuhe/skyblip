require "test_helper"

class OAuthTest < ActiveSupport::TestCase
  test "the sweep drops expired codes and tokens, and the connections left with none" do
    lapsed = granted(users(:francois), "https://claude.ai/oauth/claude-code-client-metadata")
    lapsed.issue_code code_challenge: "c" * 43, redirect_uri: "http://127.0.0.1/callback"
    lapsed.issue_tokens

    travel OAuth::Grant::REFRESH_LIFETIME + 1.day do
      held = granted(users(:amelie), "https://pi.dev/oauth/client.json")
      held.issue_tokens
      OAuth.sweep
    end

    assert_equal [ users(:amelie) ], OAuth::Grant.all.map(&:user)
    assert_equal %w[access refresh], OAuth::Token.order(:kind).pluck(:kind)
    assert_empty OAuth::Code.all
  end

  test "the sweep drops a connection whose approval lapsed, even with a token still live" do
    expired = granted(users(:francois), "https://claude.ai/oauth/claude-code-client-metadata")

    travel_to expired.approved_at + OAuth::Grant::APPROVAL_LIFETIME + 1.minute do
      expired.issue_tokens
      OAuth.sweep
    end

    assert_empty OAuth::Grant.all
    assert_empty OAuth::Token.all
  end

  private
    def granted(user, document_url)
      client = OAuth::Client.create!(document_url:, name: "Agent", redirect_uris: %w[http://127.0.0.1/callback],
                                     fetched_at: Time.current)
      OAuth::Grant.approve(user:, client:)
    end
end
