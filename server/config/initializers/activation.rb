# INFO: fc 18oct26 test.rb sets the public test key first, every other environment reads the credentials
Rails.application.config.x.activation.server_keys ||= Rails.application.credentials.dig(:activation, :server_keys).to_h
