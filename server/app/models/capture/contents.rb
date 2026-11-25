module Capture::Contents
  extend ActiveSupport::Concern

  # INFO: fc 15nov26 the session note and the boot record are the first lines blip.py and the page write
  HEAD_LINES = 4
  LINE_BYTES = 2.kilobytes
  # INFO: fc 20nov26 the widths schemas/diagnostics_layout.v1.json gives, so a forged boot record cannot put text in the firmware admins read
  VERSION_BITS = { firmware_major: 8, firmware_minor: 8, firmware_revision: 16, firmware_build: 32 }

  private
    def reads_as_a_session
      note, *rest = head_lines
      if session_note?(note)
        self.session = note[:session]
        self.records = note[:records]
        boot = rest.find { it in { type: "boot" } }
        self.firmware = firmware_of(boot) if boot
        errors.add(:profile, :inconsistent) if boot&.dig(:profile) && boot[:profile] != profile
      else
        errors.add(:body, :not_a_session)
      end
    end

    def head_lines
      reader = Zlib::GzipReader.new(StringIO.new(body.to_s))
      Array.new(HEAD_LINES) { reader.gets("\n", LINE_BYTES) }.compact.map { JSON.parse(it, symbolize_names: true) }
    rescue Zlib::Error, JSON::ParserError
      []
    end

    def session_note?(note)
      (note in { type: "session", log: "diagnostics" }) && unsigned?(note[:session], 32) && unsigned?(note[:records], 32)
    end

    def firmware_of(boot)
      if VERSION_BITS.all? { |field, bits| unsigned?(boot[field], bits) }
        major, minor, revision, build = boot.values_at(*VERSION_BITS.keys)
        "#{major}.#{minor}.#{revision}+#{build}"
      end
    end

    def unsigned?(value, bits) = value.is_a?(Integer) && value.between?(0, 2**bits - 1)
end
