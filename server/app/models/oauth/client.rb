class OAuth::Client < ApplicationRecord
  AUTH_METHOD = "none"
  FRESH_FOR = 1.day
  LOOPBACK_HOSTS = %w[127.0.0.1 localhost].freeze

  has_many :grants, dependent: :destroy

  class << self
    def identified_by(document_url)
      client = find_or_initialize_by(document_url: OAuth::Client::Document.allowed(document_url))

      if client.fresh?
        client
      else
        client.read_its_document
      end
    end

    def listed?(document_url) = document_url.in?(Rails.configuration.x.oauth_client_ids)

    def loopback?(uri) = uri.scheme == "http" && uri.host.in?(LOOPBACK_HOSTS)

    def hosted_callback?(uri) = uri.to_s.in?(Rails.configuration.x.oauth_hosted_callbacks)
  end

  def fresh? = persisted? && fetched_at.after?(FRESH_FOR.ago)

  def listed? = self.class.listed?(document_url)

  def read_its_document
    document = OAuth::Client::Document.fetch(document_url)
    update! name: document.name, client_uri: document.client_uri, redirect_uris: document.redirect_uris,
            fetched_at: Time.current
    self
  end

  def redirects_to?(candidate)
    uri = parsed(candidate)

    uri.present? && redirect_uris.any? { |registered| same_redirect?(parsed(registered), uri) }
  end

  def redirect_host(candidate)
    uri = parsed(candidate)

    self.class.loopback?(uri) ? "this computer" : uri.host
  end

  private
    def parsed(uri)
      URI.parse(uri.to_s)
    rescue URI::InvalidURIError
      nil
    end

    # INFO: fc 13oct26 RFC 8252 7.3: a native client listens on whatever loopback port is free, so only the port may differ
    def same_redirect?(registered, candidate)
      if self.class.loopback?(registered)
        self.class.loopback?(candidate) && candidate.userinfo.nil? && candidate.fragment.nil? &&
          [ registered.host, registered.path, registered.query ] == [ candidate.host, candidate.path, candidate.query ]
      else
        registered.to_s == candidate.to_s
      end
    end
end
