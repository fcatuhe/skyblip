# INFO: fc 18oct26 only skyblip.eu activates a unit: a local server or a test signs with the public test key no device build trusts
Rails.application.config.x.activation.server_keys =
  if Rails.env.local?
    { test: "5d23819c3300d895cd6ca317789db5cba92adaf4ef7b5fbe00cbc8d946d243d5" }
  else
    Rails.application.credentials.dig(:activation, :server_keys).to_h
  end
