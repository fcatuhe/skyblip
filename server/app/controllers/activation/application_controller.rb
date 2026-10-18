class Activation::ApplicationController < ApplicationController
  allow_unauthenticated_access
  skip_around_action :switch_locale

  rescue_from(Activation::Refusal) { |refusal| render json: { error: refusal.reason }, status: refusal.status }

  private
    def rate_limited
      render json: { error: :rate_limited }, status: :too_many_requests
    end
end
