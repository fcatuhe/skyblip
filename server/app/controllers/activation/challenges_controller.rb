class Activation::ChallengesController < Activation::ApplicationController
  rate_limit to: 10, within: 1.minute, by: :network_pseudonym, with: :rate_limited
  wrap_parameters :challenge, include: %i[unit_nonce server_key_id]

  def create
    challenge = Activation::Challenge.issue(challenge_params)
    render json: { server_nonce: challenge.server_nonce, signature: challenge.signature }, status: :created
  end

  private
    def challenge_params
      params.expect(challenge: %i[unit_nonce server_key_id])
    end
end
