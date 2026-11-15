class Unit::HearingJob < ApplicationJob
  limits_concurrency to: 1, key: "unit-hearing"

  def perform(uuids, ip_address)
    Unit.hear_now(uuids, ip_address:)
  end
end
