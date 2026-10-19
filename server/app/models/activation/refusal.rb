class Activation::Refusal < StandardError
  STATUSES = {
    invalid: :unprocessable_content,
    unknown_server_key: :unprocessable_content,
    expired: :unprocessable_content,
    bad_signature: :unprocessable_content,
    other_key: :conflict,
    revoked_key: :conflict,
    daily_cap: :service_unavailable,
    no_serial: :service_unavailable
  }

  attr_reader :reason

  def initialize(reason)
    @reason = reason
    super(reason.to_s)
  end

  def status = STATUSES.fetch(reason)
end
