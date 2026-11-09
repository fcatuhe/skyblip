# INFO: fc 18oct26 only skyblip.eu signs for units: a local server or a test signs with the public test key no device build trusts
Rails.application.config.x.identity.server_private_key =
  if Rails.env.local?
    "5d23819c3300d895cd6ca317789db5cba92adaf4ef7b5fbe00cbc8d946d243d5"
  else
    Rails.application.credentials.dig(:identity, :server_private_key)
  end

# INFO: fc 09nov26 checked at boot, so a malformed seed stops the deploy instead of breaking the admin pages
Rails.application.config.after_initialize do
  Identity::ServerKey.current if Identity::ServerKey.configured?
end
