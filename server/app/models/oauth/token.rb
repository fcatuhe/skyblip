class OAuth::Token < ApplicationRecord
  KINDS = %w[access refresh].freeze

  belongs_to :grant

  enum :kind, KINDS.index_by(&:itself)

  scope :live, -> { where(spent_at: nil, expires_at: Time.current..) }
  scope :expired, -> { where(expires_at: ...Time.current) }

  class << self
    def issue(grant, kind, expires_at)
      OAuth.mint.tap do |token|
        grant.tokens.create! kind:, digest: OAuth.digest(token), expires_at:
      end
    end

    def verify(token)
      held = access.live.find_by(digest: OAuth.digest(token))

      if held && held.grant.resource == OAuth.resource && held.grant.client.listed?
        held.grant.used
        held.verified(token)
      else
        raise MCP::Server::OAuth::InvalidTokenError, "The access token is unknown, expired or revoked"
      end
    end

    def renew(token, client_id:, resource: nil)
      held = refresh.find_by(digest: OAuth.digest(token))

      if held && held.grant.client.document_url == client_id && held.grant.client.listed?
        held.rotate(resource:)
      else
        raise OAuth::Error.invalid_grant("The refresh token is unknown")
      end
    end

    def revoke(token, client_id:)
      held = find_by(digest: OAuth.digest(token))

      if held && held.grant.client.document_url == client_id
        held.revoke
      end
    end
  end

  def verified(token)
    MCP::Server::OAuth::AccessToken.new(
      token:, client_id: grant.client.document_url, scopes: grant.scope.split, expires_at: expires_at.to_i,
      subject: grant.user_id, issuer: OAuth.issuer, audience: grant.resource, resource: grant.resource)
  end

  def revoke
    if refresh?
      grant.revoke
    else
      destroy!
    end
  end

  def rotate(resource: nil)
    if spent_at
      grant.revoke
      raise OAuth::Error.invalid_grant("The refresh token was used before, so the connection is revoked")
    elsif expires_at.past?
      raise OAuth::Error.invalid_grant("The refresh token has expired")
    elsif resource.present? && resource != grant.resource
      raise OAuth::Error.new("invalid_target", "skyBlip issues tokens for #{grant.resource} alone")
    elsif (tokens = transaction { grant.issue_tokens if spent? })
      tokens
    elsif (current = self.class.find_by(id:))
      current.rotate(resource:)
    else
      raise OAuth::Error.invalid_grant("The refresh token is unknown")
    end
  end

  private
    # INFO: fc 13oct26 concurrency: spending and the new pair commit together, so a racing revocation takes the pair. A second use revokes the grant
    def spent? = self.class.where(id:, spent_at: nil).update_all(spent_at: Time.current) == 1
end
