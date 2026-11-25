class Mcps::ServerCardsController < ActionController::API
  include Tracing

  SCHEMA = "https://static.modelcontextprotocol.io/schemas/v1/server-card.schema.json"
  MEDIA_TYPE = "application/mcp-server-card+json"

  rate_limit to: 60, within: 1.minute, with: -> { head :too_many_requests, retry_after: 1.minute.to_i }

  def show
    response.headers.merge! "Access-Control-Allow-Origin" => "*", "Access-Control-Allow-Methods" => "GET",
      "Access-Control-Allow-Headers" => "Content-Type, If-None-Match", "Access-Control-Expose-Headers" => "ETag"
    expires_in 1.hour, public: true

    if stale?(etag: card, public: true)
      render json: card, content_type: MEDIA_TYPE
    end
  end

  private
    def card
      { "$schema": SCHEMA, name: McpsController::NAME, version: McpsController::VERSION, title: McpsController::TITLE,
        description: McpsController::DESCRIPTION, websiteUrl: Rails.configuration.x.public_url,
        icons: ModelContextProtocol::ICONS.map { it.transform_keys { it.to_s.camelize(:lower) } },
        repository: { url: Rails.configuration.x.repository_url, source: "github", subfolder: "server" },
        remotes: [ { type: "streamable-http", url: Content.canonical_url(mcp_path),
                     supportedProtocolVersions: MCP::Configuration::SUPPORTED_STABLE_PROTOCOL_VERSIONS } ] }
    end
end
