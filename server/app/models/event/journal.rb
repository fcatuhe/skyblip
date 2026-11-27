class Event::Journal
  BROWSER = /\A[0-9A-HJKMNP-TV-Z]{8}\z/
  UUID = /\A\h{8}-\h{4}-\h{4}-\h{4}-\h{12}\z/
  DEVICE = /\A(?:go|bravo|activate)-\h{1,8}\z/
  PATH = %r{\A(?:[a-z][a-z0-9+.-]*://[^/?#]*)?(/[^?#\s]*)}i
  ASSET_DIGEST = /-\h{8,}(?=\.js\z)/
  ENTRIES = 100
  # INFO: fc 20nov26 a JSON number has no size, and every counter the firmware or the browser sends fits in a JavaScript number
  NUMBERS = -(2**53 - 1)..(2**53 - 1)
  OLDEST = 30.days
  # INFO: fc 27nov26 a post takes seconds on a poor network, so only a clock off by more than that is moved, and a timeline keeps its order
  CLOCK_TOLERANCE = 1.minute

  WORDS = %w[navigation control kind error controller filter phase product running track image imu settings trust flight power_level
             key during task version advice outcome cause tried from to check swap proof reset previous browser_name os_name language
             sha reason staged_key unit_key bootloader_keys device_id variant firmware board kind profile build last_command].freeze
  COUNTS = %w[load_ms line column connect_ms linked_ms battery_percent lasted_ms sent_bytes total_bytes request_error boots
              probation_boots browser_major bluetooth_tag usb_vendor usb_product hw_model write_ms code].freeze
  FLAGS = %w[bluetooth standalone online storage mobile returned expected swap_powered activated charging full upgrade confirmed report release trusted saved].freeze
  TEXTS = { "message" => 300, "detail" => 200 }.freeze
  PATHS = %w[file].freeze
  LISTS = %w[frames].freeze
  # INFO: fc 15nov26 the diagnostics dump: groups of counters and states, never a position or an address
  DUMPS = %w[dump].freeze
  DUMP_NAME = /\A[a-z][a-z0-9_]{0,39}\z/
  DUMP_GROUPS = 10
  DUMP_KEYS = 40

  attr_reader :browser

  def initialize(browser:, entries:, sent_at: nil, user: nil, request_id: nil, ip_address: nil)
    @browser = browser.to_s[BROWSER]
    @entries = Array(entries).first(ENTRIES)
    @skew = skew_of(sent_at)
    @user = user
    @request_id = request_id
    @ip_address = ip_address
  end

  def valid? = browser.present?

  # INFO: fc 14nov26 a journal is posted again until it is answered, so an entry's uuid keeps it once however often it arrives
  def record
    rows = @entries.filter_map { row_of(it) }
    if rows.any?
      Event.insert_all(rows, unique_by: :uuid)
      Unit.hear_later(rows.pluck(:uuid), ip_address: @ip_address)
    end
    rows.size
  end

  private
    def row_of(entry)
      entry = entry.to_h.stringify_keys
      catalogued = Event::Catalog.browser(entry["name"].to_s)
      uuid = entry["id"].to_s[UUID]

      if catalogued && uuid
        payload = told(entry["properties"], catalogued.keys)
        payload[:fingerprint] = fingerprint(payload) if catalogued.name == "browser.failed"
        { name: catalogued.name, uuid:, payload:, context: context_of(entry), occurred_at: occurred_at(entry["at"]),
          user_id: @user&.id, request_id: @request_id }
      end
    end

    def context_of(entry)
      { browser:, page: entry["page"].to_s[UUID], seq: Integer(entry["seq"], exception: false), path: path_of(entry["path"]),
        device: entry["device"].to_s[DEVICE] }.compact
    end

    def told(properties, keys)
      if properties.is_a?(Hash)
        properties.stringify_keys.slice(*keys).to_h { |key, value| [ key.to_sym, kept(key, value) ] }.compact
      else
        {}
      end
    end

    def kept(key, value)
      case key
      when *WORDS then value.to_s.first(100) if value.is_a?(String)
      when *COUNTS then Integer(value, exception: false).then { it if number?(it) }
      when *FLAGS then value if value == true || value == false
      when *TEXTS.keys then value.to_s.truncate(TEXTS[key]) if value.is_a?(String)
      when *PATHS then path_of(value)
      when *LISTS then Array(value).grep(String).first(5).map { it.truncate(200) } if value.is_a?(Array)
      when *DUMPS then dump_of(value)
      end
    end

    def dump_of(value)
      if value.is_a?(Hash)
        value.stringify_keys.select { |group, _| group.match?(DUMP_NAME) }.first(DUMP_GROUPS).to_h do |group, fields|
          [ group, fields.is_a?(Hash) ? dump_fields(fields.stringify_keys) : {} ]
        end
      end
    end

    def dump_fields(fields)
      fields.select { |key, _| key.match?(DUMP_NAME) }.first(DUMP_KEYS).to_h { |key, field| [ key, dump_value(field) ] }.compact
    end

    def dump_value(field)
      case field
      when Integer then field if number?(field)
      when true, false then field
      when String then field.first(40)
      when Array then field.select { number?(it) }.first(16)
      end
    end

    def number?(value) = value.is_a?(Integer) && NUMBERS.cover?(value)

    def path_of(url) = (url[PATH, 1]&.first(300) if url.is_a?(String))

    def occurred_at(at)
      (Time.iso8601(at.to_s) + @skew).clamp(OLDEST.ago, Time.current)
    rescue ArgumentError
      Time.current
    end

    # INFO: fc 27nov26 an entry's time is the browser's clock, so the gap between its clock and ours when it posts moves every entry onto ours
    def skew_of(sent_at)
      skew = Time.current - Time.iso8601(sent_at.to_s)
      skew.abs > CLOCK_TOLERANCE ? skew : 0
    rescue ArgumentError
      0
    end

    # INFO: fc 14nov26 the same failure differs by the numbers in its message and by the digest of the asset it came from
    def fingerprint(payload)
      message = payload[:message].to_s.gsub(/\d+/, "N").gsub(/(["'`]).*?\1/, "\\1\\1")
      file = payload[:file].to_s.sub(ASSET_DIGEST, "")
      OpenSSL::Digest::SHA256.hexdigest([ payload[:kind], payload[:error], message, file ].join("|")).first(16)
    end
end
