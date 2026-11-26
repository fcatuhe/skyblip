class GetCaptureTool < ApplicationTool
  tool_name "get_capture"
  title "Download a capture"
  description "Download a diagnostics capture a pilot sent from a device page, as the Captures page in the admin area does: " \
              "its device, profile, session, record count and firmware, then the file itself, the gzipped NDJSON " \
              "scripts/blip.py fetch writes, as a base64 blob. Decode the blob to a .ndjson.gz file and read it with " \
              "scripts/blip.py decode or scripts/power_budget.py, never in context: a full capture is up to 7 MB of NDJSON. " \
              "list_events with the name capture.received gives each capture's capture_id. A full capture holds the unit's " \
              "track and every aircraft it heard, so each download is logged with who made it. For admins only."
  input_schema properties: { capture_id: { type: "integer", minimum: 1, description: "The capture's id, as capture.received gives it" } },
    required: [ "capture_id" ]
  annotations(**READING)

  def self.call(server_context:, capture_id:)
    if (reader = admin(server_context))
      if (capture = Capture.find_by(id: capture_id))
        file_of(capture, reader)
      else
        refused "No capture has the id #{capture_id.to_s.truncate(20)}"
      end
    else
      admins_only
    end
  end

  def self.file_of(capture, reader)
    facts = capture.slice(:id, :device, :profile, :session, :records, :bytes, :firmware, :filename)
      .merge(received_at: capture.created_at.utc.iso8601, kept_until: capture.kept_until.utc.iso8601)
    blob = MCP::Resource::BlobContents.new(data: Base64.strict_encode64(capture.download(by: reader)),
      uri: Rails.application.routes.url_helpers.admin_capture_url(capture), mime_type: "application/gzip")

    MCP::Tool::Response.new([ { type: "text", text: JSON.pretty_generate(facts) }, MCP::Content::EmbeddedResource.new(blob).to_h ])
  end
  private_class_method :file_of
end
