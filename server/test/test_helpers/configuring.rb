module Configuring
  def with_config(**overrides)
    held = overrides.keys.index_with { |knob| Rails.configuration.x.public_send(knob) }
    overrides.each { |knob, value| Rails.configuration.x.public_send("#{knob}=", value) }

    yield
  ensure
    held.each { |knob, value| Rails.configuration.x.public_send("#{knob}=", value) }
  end
end
