# INFO: fc 14nov26 Event reloads in development, so the reporters hold this module and it looks Event up on each call
module EventLog
  def self.emit(event) = Event.emit(event)

  def self.report(error, **) = Event.report(error, **)
end

Rails.event.subscribe EventLog
Rails.error.subscribe EventLog
