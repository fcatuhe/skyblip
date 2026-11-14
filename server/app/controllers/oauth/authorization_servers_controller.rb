class OAuth::AuthorizationServersController < ActionController::API
  include Tracing

  def show
    render json: OAuth.server_metadata
  end
end
