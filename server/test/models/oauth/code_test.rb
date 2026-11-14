require "test_helper"

class OAuth::CodeTest < ActiveSupport::TestCase
  REDIRECT = "http://127.0.0.1/callback"

  self.use_transactional_tests = false

  teardown do
    OAuth::Client.destroy_all
    Event.delete_all
  end

  # OAuth 2.1 4.1.3: the thief and the agent redeeming at the same moment is the stolen-code case itself.
  test "a code redeemed by another request after it was loaded revokes the tokens of its grant" do
    grant = approved_grant
    grant.issue_tokens
    loaded = OAuth::Code.find_by!(digest: OAuth.digest(issue_code(grant)))

    OAuth::Code.where(id: loaded.id).update_all(redeemed_at: Time.current)

    error = assert_raises(OAuth::Error) do
      loaded.redeem(client_id: ConnectingAgents::CLAUDE_CODE, redirect_uri: REDIRECT, code_verifier:)
    end
    assert_equal "invalid_grant", error.code
    assert_empty grant.tokens.reload
  end

  # The thief may be the one whose redemption wins, so the tokens it was given go too.
  test "a code redeemed twice at once leaves the winner no tokens" do
    grant = approved_grant
    code = issue_code(grant)
    racing, paused = Queue.new, Queue.new

    loser = Thread.new do
      racing.pop
      ActiveRecord::Base.connection_pool.with_connection { redeem(code) }
    rescue OAuth::Error => error
      error
    ensure
      paused << :done
    end

    hold_winner_until_loser_blocks_or_ends = Object.new.tap do |listener|
      listener.define_singleton_method(:start) do |_name, _id, payload|
        paused << :waiting if Thread.current == loser && payload[:sql].start_with?("BEGIN")
      end
      listener.define_singleton_method(:finish) do |_name, _id, payload|
        if Thread.current != loser && payload[:sql].start_with?(%(UPDATE "oauth_codes"))
          racing << :go
          paused.pop
        end
      end
    end

    won = ActiveSupport::Notifications.subscribed(hold_winner_until_loser_blocks_or_ends, "sql.active_record") { redeem(code) }

    assert won[:access_token]
    assert_equal "invalid_grant", loser.value.code
    assert_empty grant.tokens.reload
  end

  # The thief who redeemed first may be refreshing when the agent presents the code, and keeps nothing either.
  test "a code reused while its tokens refresh revokes the refreshed tokens too" do
    grant = approved_grant
    code = issue_code(grant)
    refresh_token = redeem(code)[:refresh_token]
    starting, spent, reused = Queue.new, Queue.new, Queue.new
    reuser, refresh_holds_lock = Thread.current, false

    refreshing = Thread.new do
      starting.pop
      ActiveRecord::Base.connection_pool.with_connection { OAuth::Token.renew(refresh_token, client_id: ConnectingAgents::CLAUDE_CODE) }
    end

    hold_refresh_until_reuse_blocks_or_ends = Object.new.tap do |listener|
      listener.define_singleton_method(:start) do |_name, _id, payload|
        if payload[:sql].start_with?("BEGIN")
          refresh_holds_lock = true if Thread.current == refreshing
          reused << :waiting if Thread.current == reuser && refresh_holds_lock
        end
      end
      listener.define_singleton_method(:finish) do |_name, _id, payload|
        if Thread.current == refreshing && payload[:sql].start_with?(%(UPDATE "oauth_tokens"))
          spent << :spent
          reused.pop
        end
      end
    end

    renewed = ActiveSupport::Notifications.subscribed(hold_refresh_until_reuse_blocks_or_ends, "sql.active_record") do
      starting << :go
      spent.pop
      assert_raises(OAuth::Error) { redeem(code) }
      reused << :done
      refreshing.value
    end

    assert_not OAuth::Grant.exists?(grant.id)
    assert_raises(MCP::Server::OAuth::InvalidTokenError) { OAuth::Token.verify(renewed[:access_token]) }
    assert_raises(OAuth::Error) { OAuth::Token.renew(renewed[:refresh_token], client_id: ConnectingAgents::CLAUDE_CODE) }
  end

  private
    def approved_grant
      OAuth::Grant.approve(user: users(:francois), client: OAuth::Client.create!(
        document_url: ConnectingAgents::CLAUDE_CODE, name: "Claude Code", redirect_uris: [ REDIRECT ], fetched_at: Time.current))
    end

    def code_verifier = @code_verifier ||= SecureRandom.urlsafe_base64(48)

    def issue_code(grant) = grant.issue_code(code_challenge: OAuth::Code.challenge_of(code_verifier), redirect_uri: REDIRECT)

    def redeem(code)
      OAuth::Code.redeem(code, client_id: ConnectingAgents::CLAUDE_CODE, redirect_uri: REDIRECT, code_verifier:)
    end
end
