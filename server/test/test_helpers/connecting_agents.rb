module ConnectingAgents
  extend ActiveSupport::Concern

  CLAUDE_CODE = "https://claude.ai/oauth/claude-code-client-metadata"
  CODEX = "https://chatgpt.com/oauth/codex/client.json"
  PI = "https://pi.dev/oauth/client.json"
  CLAUDE = "https://claude.ai/oauth/mcp-oauth-client-metadata"
  LOOPBACK = "http://127.0.0.1:53682/callback"
  HOSTED = "https://claude.ai/api/mcp/auth_callback"

  included do
    setup { Rails.cache.clear }
  end

  def consent_query(client_id:, redirect_uri: LOOPBACK, verifier: code_verifier, **overrides)
    { response_type: "code", client_id:, redirect_uri:, state: "st4te", scope: OAuth::SCOPE, resource: OAuth.resource,
      code_challenge: OAuth::Code.challenge_of(verifier), code_challenge_method: "S256", **overrides }
  end

  def code_verifier = @code_verifier ||= SecureRandom.urlsafe_base64(48)

  def approve(client_id:, redirect_uri: LOOPBACK, as: users(:francois))
    sign_in_as as
    vcr("oauth/#{URI(client_id).host}") do
      post oauth_authorization_path, params: consent_query(client_id:, redirect_uri:)
    end

    Rack::Utils.parse_query URI(response.location).query
  end

  def redeem(code, client_id:, redirect_uri: LOOPBACK, verifier: code_verifier)
    post oauth_token_path, params: { grant_type: "authorization_code", code:, client_id:, redirect_uri:,
                                     code_verifier: verifier, resource: OAuth.resource }
    response.parsed_body
  end

  def connect(client_id:, redirect_uri: LOOPBACK, as: users(:francois))
    redeem approve(client_id:, redirect_uri:, as:).fetch("code"), client_id:, redirect_uri:
  end

  def logged_by_controllers
    log = StringIO.new
    held, ActionController::Base.logger = ActionController::Base.logger, ActiveSupport::Logger.new(log)
    yield
    log.string
  ensure
    ActionController::Base.logger = held
  end

  def mcp(method, params = {}, token:)
    post mcp_path, params: { jsonrpc: "2.0", id: 1, method:, params: }.to_json,
      headers: { "Content-Type" => "application/json", "Accept" => "application/json, text/event-stream",
                 "MCP-Protocol-Version" => "2025-06-18", **({ "Authorization" => "Bearer #{token}" } if token) }
  end

  def call_tool(name, arguments = {}, token:)
    mcp("tools/call", { name:, arguments: }, token:)
    response.parsed_body.dig("result", "content", 0, "text")
  end
end
