module OAuth
  SCOPE = "read"

  class Error < StandardError
    UNAUTHORIZED = %w[invalid_client].freeze

    attr_reader :code

    def self.invalid_grant(description) = new("invalid_grant", description)

    def initialize(code, description)
      super(description)
      @code = code
    end

    def status = code.in?(UNAUTHORIZED) ? :unauthorized : :bad_request

    def to_h = { error: code, error_description: message }
  end

  class << self
    def table_name_prefix = "oauth_"

    def issuer = routes.root_url.chomp("/")

    def resource = routes.mcp_url

    def protected_resource
      MCP::Server::OAuth::ProtectedResourceMetadata.new(
        resource:, authorization_servers: [ issuer ], scopes_supported: [ SCOPE ], resource_name: "skyBlip")
    end

    def server_metadata
      { issuer: issuer,
        authorization_endpoint: routes.new_oauth_authorization_url,
        token_endpoint: routes.oauth_token_url,
        revocation_endpoint: routes.oauth_revocation_url,
        scopes_supported: [ SCOPE ],
        response_types_supported: [ OAuth::Authorization::RESPONSE_TYPE ],
        grant_types_supported: %w[authorization_code refresh_token],
        code_challenge_methods_supported: [ OAuth::Code::CHALLENGE_METHOD ],
        token_endpoint_auth_methods_supported: [ OAuth::Client::AUTH_METHOD ],
        revocation_endpoint_auth_methods_supported: [ OAuth::Client::AUTH_METHOD ],
        client_id_metadata_document_supported: true,
        authorization_response_iss_parameter_supported: true }
    end

    def sweep
      OAuth::Code.expired.delete_all
      OAuth::Token.expired.delete_all
      OAuth::Grant.lapsed.destroy_all
      OAuth::Grant.expired.destroy_all
    end

    def mint = SecureRandom.base58(43)

    def digest(secret) = OpenSSL::Digest::SHA256.hexdigest(secret.to_s)

    private
      def routes = Rails.application.routes.url_helpers
  end
end
