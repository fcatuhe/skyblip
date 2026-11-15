module Capture::Contents
  extend ActiveSupport::Concern

  # INFO: fc 15nov26 the session note and the boot record are the first lines blip.py and the page write
  HEAD_LINES = 4
  LINE_BYTES = 2.kilobytes

  private
    def reads_as_a_session
      note, *rest = head_lines
      if note&.dig("type") == "session" && note["log"] == "diagnostics"
        self.session = note["session"]
        self.records = note["records"]
        boot = rest.find { it["type"] == "boot" }
        self.firmware = firmware_of(boot) if boot
        errors.add(:profile, :inconsistent) if boot&.dig("profile") && boot["profile"] != profile
      else
        errors.add(:body, :not_a_session)
      end
    end

    def head_lines
      reader = Zlib::GzipReader.new(StringIO.new(body.to_s))
      Array.new(HEAD_LINES) { reader.gets("\n", LINE_BYTES) }.compact.map { JSON.parse(it) }
    rescue Zlib::Error, JSON::ParserError
      []
    end

    def firmware_of(boot)
      "#{boot["firmware_major"]}.#{boot["firmware_minor"]}.#{boot["firmware_revision"]}+#{boot["firmware_build"]}"
    end
end
