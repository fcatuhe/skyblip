class OAuth::ProtectedResourcesController < ActionController::API
  include Tracing

  def show
    render json: OAuth.protected_resource
  end
end
