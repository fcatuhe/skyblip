class OAuth::TokensController < ActionController::API
  include NetworkPseudonym
  include Tracing

  GRANT_TYPES = %w[authorization_code refresh_token].freeze

  rate_limit to: 30, within: 1.minute, by: :network_pseudonym, with: -> { head :too_many_requests, retry_after: 1.minute.to_i }

  rescue_from OAuth::Error do |error|
    Rails.event.notify "oauth_token.refused", error: error.code, grant_type: form[:grant_type].presence_in(GRANT_TYPES) || "other"
    response.headers["Cache-Control"] = "no-store"
    render json: error.to_h, status: error.status
  end

  def create
    response.headers["Cache-Control"] = "no-store"
    render json: issued
  end

  private
    def issued
      case form[:grant_type]
      when "authorization_code"
        OAuth::Code.redeem form[:code], client_id: form[:client_id], redirect_uri: form[:redirect_uri],
                                       code_verifier: form[:code_verifier], resource: form[:resource]
      when "refresh_token"
        OAuth::Token.renew form[:refresh_token], client_id: form[:client_id], resource: form[:resource]
      else
        raise OAuth::Error.new("unsupported_grant_type", "skyBlip takes #{GRANT_TYPES.to_sentence}")
      end
    end

    # INFO: fc 14nov26 RFC 6749 3.2: the body alone, so no code or token lands in a proxy's access log
    def form = request.request_parameters
end
