class Attestation::Refusal < StandardError
  STATUSES = {
    invalid: :unprocessable_content,
    expired: :unprocessable_content,
    bad_signature: :unprocessable_content,
    unknown_unit: :not_found,
    no_active_key: :conflict
  }

  attr_reader :reason

  def initialize(reason)
    @reason = reason
    super(reason.to_s)
  end

  def status = STATUSES.fetch(reason)
end
