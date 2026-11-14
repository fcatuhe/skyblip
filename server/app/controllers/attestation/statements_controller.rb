class Attestation::StatementsController < Attestation::ApplicationController
  rate_limit to: 20, within: 1.minute, by: -> { Unit.network_of(request.remote_ip) || request.remote_ip }, with: :rate_limited
  wrap_parameters :statement, include: %i[device_id version release server_nonce signature]

  def create
    firmware = Attestation::Statement.new(statement_params).record
    render json: { version: firmware.version, release: firmware.release }, status: :created
  end

  private
    def statement_params
      params.expect(statement: %i[device_id version release server_nonce signature])
    end
end
