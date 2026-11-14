class ListEventsTool < ApplicationTool
  MAX_LIMIT = 2_000

  tool_name "list_events"
  title "List skyBlip's events"
  description "The latest events skyBlip recorded, oldest first, as JSON lines: the catalog of event names comes first " \
              "unless a name is given. Narrow to a name or a prefix (device., browser.failed), a device, the support code " \
              "a pilot gives (ABCD-EFGH), an error's fingerprint from list_errors, or a request id. A device's or a " \
              "browser's events lined up by time say what happened to its pilot. For admins only, and each read is logged."
  input_schema properties: {
    name: { type: "string", description: "An event name, or the start of one" },
    device: DEVICE,
    support_code: { type: "string", description: "The support code a pilot reads in the briefing app's settings, ABCD-EFGH" },
    fingerprint: { type: "string", description: "An error's fingerprint, as list_errors gives it" },
    request_id: { type: "string", description: "A request's id, as an event's context gives it" },
    limit: { type: "integer", minimum: 1, maximum: MAX_LIMIT, description: "How many, #{Event::LIMIT} unless said" }
  }
  annotations(**READING)

  def self.call(server_context:, name: nil, device: nil, support_code: nil, fingerprint: nil, request_id: nil, limit: Event::LIMIT)
    if (reader = admin(server_context))
      browser = support_code&.delete("-")&.upcase
      events = Event.list(by: reader, name:, device:, browser:, fingerprint:, request_id:, last: limit.to_i.clamp(1, MAX_LIMIT))
      text [ *(catalog if name.blank?), *events.map(&:to_line) ].join("\n")
    else
      admins_only
    end
  end

  def self.catalog
    Event::Catalog.entries.map { { event: it.name, group: it.group, from: it.from, says: it.says, answers: it.answers }.to_json }
  end
  private_class_method :catalog
end
