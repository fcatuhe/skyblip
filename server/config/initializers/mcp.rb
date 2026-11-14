# INFO: fc 14nov26 a failing tool answers the agent a bare internal error, so the error itself goes to the log and Rails.error
MCP.configure do |config|
  config.exception_reporter = ->(error, context) do
    Rails.logger.error error.full_message(highlight: false)
    Rails.error.report error, context:
  end

  config.around_request = ->(data, &request) do
    began = Process.clock_gettime(Process::CLOCK_MONOTONIC, :millisecond)
    request.call
  ensure
    Rails.event.notify "mcp.answered", method: data[:method], tool: data[:tool_name], error: data[:error]&.to_s,
      duration_ms: Process.clock_gettime(Process::CLOCK_MONOTONIC, :millisecond) - began
  end
end
