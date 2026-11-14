class Event < EventsRecord
  TRACES = %w[
    action_controller.request_completed action_controller.rescue_from_handled
    active_job.started active_job.completed active_job.retry_scheduled active_job.retry_stopped active_job.discarded
  ].freeze
  FRAMEWORKS = %w[action_cable action_controller action_dispatch action_mailer action_view active_job active_record active_storage active_support].freeze
  UNTOLD = %i[arguments exception_backtrace exception_message params].freeze
  # INFO: fc 14nov26 every open briefing posts its journal every few seconds, so only the refused posts are worth a row
  BUSY = %w[EventsController Ahoy::EventsController Ahoy::VisitsController].freeze
  CHATTER = %w[browser.viewed browser.clicked].freeze
  ERRORS = %w[error.reported browser.failed].freeze
  LIMIT = 200

  scope :short_lived, -> { where(name: TRACES + CHATTER) }
  scope :errors, -> { where(name: ERRORS) }
  scope :named_like, ->(prefix) { where(arel_table[:name].matches("#{sanitize_sql_like(prefix)}%", "\\")) }
  scope :since, ->(time) { where(occurred_at: time..) }
  scope :expired, -> do
    short_lived.where(occurred_at: ...Rails.configuration.x.tracker_lifetime.ago)
      .or(where(occurred_at: ...Rails.configuration.x.retention.ago))
  end

  class << self
    def emit(event)
      if kept?(event)
        ActiveRecord.after_all_transactions_commit do
          Rails.error.handle(source: "event_log") do
            record event[:name], event[:payload].to_h.except(*UNTOLD), context: event[:context].to_h,
              source: source_of(event), occurred_at: Time.at(0, event[:timestamp], :nsec)
          end
        end
      end
    end

    def report(error, handled:, severity:, context:, source:)
      record_now "error.reported", Event::ErrorReport.new(error, handled:, severity:, context:, source:).to_h
    end

    def prune = expired.in_batches.delete_all

    def field(path)
      column, key = path.to_s.split(".", 2)

      if key
        Arel::Nodes::NamedFunction.new("json_extract", [ arel_table[column], Arel::Nodes.build_quoted("$.#{key}") ])
      else
        arel_table[column]
      end
    end

    def tail(name: nil, device: nil, browser: nil, fingerprint: nil, request_id: nil, last: LIMIT)
      events = all
      events = events.named_like(name) if name.present?
      events = events.where(device:) if device.present?
      events = events.where(browser:) if browser.present?
      events = events.where(fingerprint:) if fingerprint.present?
      events = events.where(request_id:) if request_id.present?
      events.order(:occurred_at, :id).last(last)
    end

    def list(by:, **filters)
      tail(**filters).tap do
        record_now "event.listed", { by_id: by.id, **filters.slice(:device, :browser, :fingerprint, :request_id).compact_blank,
          prefix: (filters[:name] if Event::Catalog.prefix?(filters[:name].to_s)), limit: filters[:last] }.compact
      end
    end

    def record_now(name, payload) = record(name, payload, context: Rails.event.context, source: nil, occurred_at: Time.current)

    private
      def kept?(event)
        name, payload = event[:name], event[:payload]

        if name == "action_controller.request_completed"
          !payload[:controller].in?(BUSY) || payload[:status].to_i >= 400
        else
          TRACES.include?(name) || !name.start_with?(*FRAMEWORKS.map { "#{it}." })
        end
      end

      def record(name, payload, context:, source:, occurred_at:)
        create! name:, payload:, context:, source:, occurred_at:,
          user_id: context[:user_id] || Current.user&.id, request_id: context[:request_id]
      end

      def source_of(event)
        if (location = event[:source_location]) && location[:filepath].start_with?(Rails.root.to_s)
          "#{location[:filepath].delete_prefix("#{Rails.root}/")}:#{location[:lineno]}"
        end
      end
  end

  def to_line = { at: occurred_at.utc.iso8601(3), name:, device:, browser:, user_id:, payload:, context:, source: }.compact_blank.to_json
end
