class ListErrorsTool < ApplicationTool
  tool_name "list_errors"
  title "List what breaks in skyBlip"
  description "The errors of the server and the scripts browsers ran over the last days, grouped by fingerprint, latest " \
              "first: the class, the message, where it was raised, how often, on how many devices and browsers, when it " \
              "was first and last seen and the revision that raised it last. An error resolved with resolve_error is left " \
              "out until it happens again, when it comes back with reopened. Read an error's occurrences with list_events " \
              "and its fingerprint. For admins only."
  MAX_DAYS = Rails.configuration.x.retention.in_days.to_i

  input_schema properties: {
    days: { type: "integer", minimum: 1, maximum: MAX_DAYS, description: "How far back, 7 unless said" }
  }
  annotations(**READING)

  def self.call(server_context:, days: 7)
    if admin(server_context)
      since = days.to_i.clamp(1, MAX_DAYS).days.ago
      json Event::Error.since(since).reject(&:resolved?).map { it.to_h.merge(new: it.new_since?(since)) }
    else
      admins_only
    end
  end
end
