class Activation::ApplicationController < ApplicationController
  allow_unauthenticated_access
  skip_around_action :switch_locale

  rescue_from Activation::Refusal do |refusal|
    Rails.event.notify "activation.refused", reason: refusal.reason, step: controller_name.singularize
    render json: { error: refusal.reason }, status: refusal.status
  end
  rescue_from(ActionController::ParameterMissing, ActionDispatch::Http::Parameters::ParseError) { render json: { error: :invalid }, status: :unprocessable_content }

  private
    def rate_limited
      render json: { error: :rate_limited }, status: :too_many_requests
    end
end
