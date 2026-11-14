class Event::Review
  PERIODS = [ 7, 30, 90 ].freeze
  SHOWN = 10
  # INFO: fc 14nov26 the daily cap stops a flood of made-up units, so it should stay two to three times the busiest real day
  CAP_HEADROOM = 2
  CAP_TARGET = 3
  RESULTS = %w[saved capture_armed capture_stopped activated needs_full].freeze
  BUSY = %w[asking confirming uploading].freeze
  JOURNEY = %w[
    device.connected device.read device.task_ended device.installed device.noticed
    bluetooth.chooser_closed bluetooth.chooser_failed activation.stepped activation.noticed
  ].freeze

  attr_reader :since, :till

  def initialize(since: 7.days.ago, till: Time.current)
    @since, @till = since, till
  end

  def previous = self.class.new(since: since - (till - since), till: since)

  def to_h
    { window: "#{since.to_date} to #{till.to_date}", headline:, browsers:, devices:, notices:, tasks:, installs:, firmware:,
      activations:, stranded:, errors: errors.merge(problems:), requests:, jobs:, agents: }
  end

  def headline
    { browsers: browsers[:total], without_bluetooth: browsers[:without_bluetooth], devices: devices[:connected],
      installs_started: installs[:started], installs_succeeded: installs[:installed], stranded: stranded[:count],
      server_errors: errors[:server], browser_errors: errors[:browser] }
  end

  def browsers
    @browsers ||= begin
      opened = rows("browser.opened", "browser", "payload.bluetooth", "payload.standalone", "payload.browser_name", "payload.os_name")
      latest = opened.group_by { it["browser"] }.transform_values(&:last).values
      without = latest.reject { yes?(it["payload.bluetooth"]) }

      { total: latest.size, installed_app: latest.count { yes?(it["payload.standalone"]) }, without_bluetooth: without.size,
        without_bluetooth_on: top(without.map { [ it["payload.browser_name"], it["payload.os_name"] ].compact.join(" on ") }) }
    end
  end

  def devices
    @devices ||= begin
      connected = rows("device.connected", "device", "payload.connect_ms", "payload.returned")
      dropped = rows("device.disconnected", "payload.phase", "payload.expected").reject { yes?(it["payload.expected"]) }

      { connected: connected.map { it["device"] }.uniq.size, connections: connected.size,
        connect_ms: spread(connected.reject { yes?(it["payload.returned"]) }.map { it["payload.connect_ms"] }),
        dropped_while_busy: top(dropped.map { it["payload.phase"] }.select { it.in?(BUSY) }),
        choosers_closed: top(rows("bluetooth.chooser_closed", "payload.filter").map { it["payload.filter"] }),
        choosers_failed: top(rows("bluetooth.chooser_failed", "payload.error").map { it["payload.error"] }) }
    end
  end

  def notices
    rows("device.noticed", "device", "payload.key").group_by { it["payload.key"] }
      .map { |key, seen| { key:, count: seen.size, devices: seen.map { it["device"] }.uniq.size } }
      .sort_by { -it[:count] }.first(SHOWN)
  end

  def tasks
    started = rows("device.task_started", "payload.task").map { it["payload.task"] }.tally
    ended = rows("device.task_ended", "payload.task", "payload.outcome").group_by { it["payload.task"] }

    started.keys.union(ended.keys).index_with do |task|
      outcomes = Array(ended[task]).map { it["payload.outcome"] }
      { started: started[task].to_i, granted: outcomes.count("granted"), refused: top(outcomes - [ "granted" ]) }
    end
  end

  def installs
    @installs ||= begin
      installed = rows("device.installed", "payload.lasted_ms")

      { started: tasks.dig("install", :started).to_i, installed: installed.size,
        minutes: spread(installed.map { it["payload.lasted_ms"]&.fdiv(60_000)&.round(1) }),
        failed_by_cause: top(rows("device.install_reported", "payload.cause").map { it["payload.cause"] || "image_failed" }) }
    end
  end

  def firmware
    read = rows("device.read", "device", "payload.running", "payload.image", "payload.settings")
    latest = read.group_by { it["device"] }.transform_values(&:last).values

    { running: top(latest.map { it["payload.running"] }), images_not_confirmed: top(latest.map { it["payload.image"] } - [ "confirmed" ]),
      settings_fallen_back: top(latest.filter_map { it["payload.settings"] }) }
  end

  def activations
    { enrolled: within("unit.enrolled").count, daily_cap:, refused_by_server: top(rows("activation.refused", "payload.reason").map { it["payload.reason"] }),
      noticed: top(rows("activation.noticed", "payload.key").map { it["payload.key"] } - [ "activated" ]) }
  end

  def daily_cap
    cap = Unit.daily_cap
    busiest = within("unit.enrolled").pluck(:occurred_at).group_by(&:to_date).values.map(&:size).max.to_i
    advice = ("raise it to #{busiest * CAP_TARGET}: it covers less than #{CAP_HEADROOM} times the busiest day" if cap < busiest * CAP_HEADROOM)

    { cap:, busiest_day: busiest, covers: (cap.fdiv(busiest).round(1) if busiest > 0), advice: }.compact
  end

  # INFO: fc 14nov26 a pilot whose last step in the app was a failure gave up there, so that browser is worth a look before they write
  def stranded
    @stranded ||= begin
      last_ids = within(JOURNEY).where.not(browser: nil).group(:browser).maximum(:id).values
      stuck = Event.where(id: last_ids).order(occurred_at: :desc).select { failure?(it) }

      { count: stuck.size, by_reason: top(stuck.map { reason_of(it) }),
        latest: stuck.first(SHOWN).map { { browser: it.browser, device: it.device, reason: reason_of(it), at: it.occurred_at.utc.iso8601 }.compact } }
    end
  end

  def errors
    @errors ||= { server: within("error.reported").count, browser: within("browser.failed").count }
  end

  def problems
    found = Event::Problem.since(since).select { it.last_at <= till }
    { count: found.size, new: found.count { it.new_since?(since) }, latest: found.first(SHOWN).map(&:to_h) }
  end

  def requests
    answered = rows("action_controller.request_completed", "payload.controller", "payload.action", "payload.duration_ms", "payload.status")
    slowest = answered.group_by { "#{it["payload.controller"]}##{it["payload.action"]}" }
      .transform_values { spread(it.map { it["payload.duration_ms"] }) }.sort_by { -it.last[:p90].to_f }.first(5).to_h

    { count: answered.size, server_errors: answered.count { it["payload.status"].to_i >= 500 }, slowest_p90_ms: slowest }
  end

  def jobs
    { failed: top(rows("active_job.completed", "payload.job_class", "payload.exception_class").select { it["payload.exception_class"] }.map { it["payload.job_class"] }) }
  end

  def agents = { calls: top(rows("mcp.answered", "payload.tool").map { it["payload.tool"] || "other" }) }

  private
    def within(names) = Event.where(name: names, occurred_at: since..till)

    def rows(name, *fields)
      (@rows ||= {})[[ name, fields ]] ||= within(name).order(:occurred_at, :id).pluck(*fields.map { Event.field(it) })
        .map { |values| fields.zip(Array.wrap(values)).to_h }
    end

    def failure?(event)
      case event.name
      when "device.noticed" then !event.payload["key"].in?(RESULTS)
      when "device.task_ended" then event.payload["outcome"] != "granted"
      when "activation.noticed" then event.payload["key"] != "activated"
      when "bluetooth.chooser_closed", "bluetooth.chooser_failed" then true
      else false
      end
    end

    def reason_of(event)
      [ event.name, event.payload["key"] || event.payload["outcome"] || event.payload["filter"] ].compact.join(" ")
    end

    def yes?(value) = value == true || value == 1

    def top(values) = values.compact.tally.sort_by { -it.last }.first(SHOWN).to_h

    def spread(values)
      sorted = values.compact.sort

      if sorted.any?
        { count: sorted.size, p50: sorted[sorted.size / 2], p90: sorted[(sorted.size * 0.9).floor.clamp(..sorted.size - 1)] }
      else
        { count: 0 }
      end
    end
end
