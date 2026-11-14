class Attestation::ChallengesController < Attestation::ApplicationController
  rate_limit to: 20, within: 1.minute, by: -> { Unit.network_of(request.remote_ip) || request.remote_ip }, with: :rate_limited

  def create
    challenge = Attestation::Challenge.create!
    render json: { server_nonce: challenge.server_nonce }, status: :created
  end
end
