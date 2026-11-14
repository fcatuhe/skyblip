class OAuth::Client::Document
  attr_reader :name, :client_uri, :redirect_uris

  class << self
    # INFO: fc 13oct26 only listed documents are fetched, so no client_id points skyBlip inside the network or poses as a listed agent
    def allowed(document_url)
      if OAuth::Client.listed?(document_url) && URI.parse(document_url).is_a?(URI::HTTPS)
        document_url
      else
        raise refused("skyBlip does not know the client #{document_url.to_s.truncate(200).inspect}")
      end
    end

    def fetch(document_url)
      new(document_url, OAuth::Remote.json(URI.parse(document_url)))
    rescue OAuth::Remote::Unreadable => error
      raise refused(error.message)
    end

    def refused(description) = OAuth::Error.new("invalid_client", description)
  end

  def initialize(document_url, metadata)
    @document_url = document_url

    if !metadata.is_a?(Hash)
      raise refused("is not a JSON object")
    elsif metadata["client_id"] != document_url
      raise refused("names another client_id")
    elsif metadata["token_endpoint_auth_method"] != OAuth::Client::AUTH_METHOD
      raise refused("is not a public client, its token_endpoint_auth_method is not none")
    elsif metadata["client_name"].blank?
      raise refused("has no client_name")
    else
      @name = metadata["client_name"].to_s.truncate(80)
      @client_uri = metadata["client_uri"].presence&.to_s
      @redirect_uris = usable(metadata["redirect_uris"])
    end
  end

  private
    def usable(redirect_uris)
      usable = Array(redirect_uris).grep(String).select { |uri| acceptable?(uri) }

      if usable.any?
        usable
      else
        raise refused("lists no redirect_uri skyBlip sends codes to, a loopback or an allowlisted callback")
      end
    end

    def acceptable?(redirect_uri)
      uri = URI.parse(redirect_uri)

      OAuth::Client.loopback?(uri) || OAuth::Client.hosted_callback?(uri)
    rescue URI::InvalidURIError
      false
    end

    def refused(description) = self.class.refused("The client metadata document at #{@document_url} #{description}")
end
