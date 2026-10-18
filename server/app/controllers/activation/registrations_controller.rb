class Activation::RegistrationsController < Activation::ApplicationController
  rate_limit to: 10, within: 1.minute, with: :rate_limited
  wrap_parameters :registration, include: %i[device_id public_key version server_key server_nonce signature]

  def create
    registration = Activation::Registration.new(registration_params.merge(ip_address: request.remote_ip))
    render json: registration.unlock_code, status: :created
  end

  private
    def registration_params
      params.expect(registration: %i[device_id public_key version server_key server_nonce signature])
    end
end
