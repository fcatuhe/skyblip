class OAuth::Authorization
  RESPONSE_TYPE = "code"
  FIELDS = %i[response_type client_id redirect_uri state scope resource code_challenge code_challenge_method].freeze
  CHALLENGE = /\A[A-Za-z0-9\-_]{43,128}\z/
  SCOPES_ALLOWED = [ OAuth::SCOPE, "offline_access" ].freeze

  attr_reader :fields, :client, :redirect_uri

  def initialize(fields)
    @fields = fields.to_h.symbolize_keys.slice(*FIELDS).compact_blank
    @client = OAuth::Client.identified_by(@fields[:client_id])
    @redirect_uri = @fields[:redirect_uri]

    if !client.redirects_to?(redirect_uri)
      raise OAuth::Error.new("invalid_request", "The redirect_uri is not one #{client.name} registered")
    end
  end

  def refusal
    if fields[:response_type] != RESPONSE_TYPE
      OAuth::Error.new("unsupported_response_type", "skyBlip answers response_type=code alone")
    elsif !fields[:code_challenge].to_s.match?(CHALLENGE) || fields[:code_challenge_method] != OAuth::Code::CHALLENGE_METHOD
      OAuth::Error.new("invalid_request", "skyBlip requires a PKCE code_challenge, with the S256 method")
    elsif fields[:resource].present? && fields[:resource] != OAuth.resource
      OAuth::Error.new("invalid_target", "skyBlip issues tokens for #{OAuth.resource} alone")
    elsif (unknown = fields[:scope].to_s.split - SCOPES_ALLOWED).any?
      OAuth::Error.new("invalid_scope", "skyBlip grants the #{OAuth::SCOPE} scope alone, not #{unknown.join(" ")}")
    end
  end

  def refused_url = answer(**refusal.to_h)

  def approved_url(user)
    grant = OAuth::Grant.approve(user:, client:)

    answer code: grant.issue_code(code_challenge: fields[:code_challenge], redirect_uri:)
  end

  def denied_url = answer(error: "access_denied", error_description: "The person declined to connect #{client.name}")

  def redirect_host = client.redirect_host(redirect_uri)

  private
    def answer(**params)
      uri = URI.parse(redirect_uri)
      uri.query = URI.encode_www_form(URI.decode_www_form(uri.query.to_s) +
                                      { **params, state: fields[:state], iss: OAuth.issuer }.compact.to_a)
      uri.to_s
    end
end
