class Event::ErrorReport
  FRAMES = 15
  MESSAGE_LENGTH = 300
  LINE_NUMBER = /:\d+(?=:in |\z)/
  GEM_PATH = %r{\A.*/gems/(?:[^/]+/)*?([a-z0-9_-]+?)-\d[^/]*/}

  attr_reader :error

  def initialize(error, handled:, severity:, context:, source:)
    @error = error
    @handled = handled
    @severity = severity
    @context = context
    @source = source
  end

  def to_h
    { error: error.class.name, message:, fingerprint:, handled: @handled, severity: @severity.to_s, source: @source,
      cause: error.cause&.class&.name, frames:, controller: @context[:controller]&.class&.name,
      job: @context[:job]&.class&.name, revision: Rails.configuration.x.revision }.compact
  end

  # INFO: fc 14nov26 the line number moves with every edit above it, so a problem is its class and the method it was raised in
  def fingerprint = OpenSSL::Digest::SHA256.hexdigest([ error.class.name, frames.first.to_s.sub(LINE_NUMBER, "") ].join("|")).first(16)

  private
    def message = error.message.to_s.truncate(MESSAGE_LENGTH)

    def frames
      @frames ||= begin
        backtrace = error.backtrace.to_a
        own = Rails.backtrace_cleaner.clean(backtrace)
        (own.presence || backtrace.map { it.sub(GEM_PATH, "\\1: ") }).first(FRAMES)
      end
    end
end
