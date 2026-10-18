module LogCaptureHelper
  private
    def capture_log
      output = StringIO.new
      logger = ActiveSupport::Logger.new(output)
      Rails.logger.broadcast_to(logger)
      yield
      output.string
    ensure
      Rails.logger.stop_broadcasting_to(logger)
    end
end
