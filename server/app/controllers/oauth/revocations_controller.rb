class OAuth::RevocationsController < ActionController::API
  include NetworkPseudonym
  include Tracing

  rate_limit to: 30, within: 1.minute, by: :network_pseudonym

  def create
    OAuth::Token.revoke form[:token], client_id: form[:client_id]

    head :ok
  end

  private
    # INFO: fc 14nov26 RFC 7009 2.1: the body alone, so no token lands in a proxy's access log
    def form = request.request_parameters
end
