module CaptureBodies
  # The two lines blip.py and the device page open a fetched capture with, gzipped as the page sends it.
  def capture_body(session: 1785628800, records: 2, profile: "power_run", note_type: "session")
    lines = [
      { type: note_type, log: "diagnostics", session:, records:, closed: true, truncated: false },
      { log: "diagnostics", session:, type: "boot", firmware_major: 0, firmware_minor: 4, firmware_revision: 1,
        firmware_build: 1702, profile: },
      { log: "diagnostics", session:, type: "power", percent: 80 }
    ]
    ActiveSupport::Gzip.compress(lines.map { "#{it.to_json}\n" }.join)
  end
end
