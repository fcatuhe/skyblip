class Activation::RegistrationsController < Activation::ApplicationController
  rate_limit to: 10, within: 1.minute, by: -> { Unit.network_of(request.remote_ip) || request.remote_ip }, with: :rate_limited
  wrap_parameters :registration, include: %i[device_id public_key version server_key_id release product variant bluetooth_tag server_nonce signature]

  def create
    registration = Activation::Registration.new(registration_params.merge(ip_address: request.remote_ip))
    unlock_code = registration.unlock_code
    track_activation(registration.unit)
    render json: unlock_code, status: :created
  end

  private
    def registration_params
      params.expect(registration: %i[device_id public_key version server_key_id release product variant bluetooth_tag server_nonce signature])
    end

    def track_activation(unit)
      ahoy.track Ahoy::Event::UNIT_ACTIVATED, unit_id: unit.id, device: unit.slug, browser: briefing_browser unless Ahoy.api_only
    end
end
