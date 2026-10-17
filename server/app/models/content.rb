module Content
  def self.canonical_url(path, host: Rails.application.config.x.public_url)
    "#{host.delete_suffix("/")}#{path}"
  end
end
