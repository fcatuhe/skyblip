class OAuth::Code < ApplicationRecord
  CHALLENGE_METHOD = "S256"
  LIFETIME = 1.minute
  VERIFIER = /\A[A-Za-z0-9\-._~]{43,128}\z/

  belongs_to :grant

  scope :expired, -> { where(expires_at: ...Time.current) }

  class << self
    def issue(grant, code_challenge:, redirect_uri:)
      OAuth.mint.tap do |code|
        grant.codes.create! digest: OAuth.digest(code), code_challenge:, redirect_uri:, expires_at: LIFETIME.from_now
      end
    end

    def redeem(code, client_id:, redirect_uri:, code_verifier:, resource: nil)
      if (issued = find_by(digest: OAuth.digest(code)))
        issued.redeem(client_id:, redirect_uri:, code_verifier:, resource:)
      else
        raise OAuth::Error.invalid_grant("The authorization code is unknown")
      end
    end

    def challenge_of(verifier)
      Base64.urlsafe_encode64 OpenSSL::Digest::SHA256.digest(verifier.to_s), padding: false
    end
  end

  # INFO: fc 13oct26 PKCE before reuse: only a holder of the verifier revokes, a spent code from browser history cannot
  def redeem(client_id:, redirect_uri:, code_verifier:, resource: nil)
    if grant.client.document_url != client_id
      raise invalid("The authorization code was issued to another client")
    elsif expires_at.past?
      raise invalid("The authorization code has expired")
    elsif redirect_uri != self.redirect_uri
      raise invalid("The redirect_uri is not the one the code was sent to")
    elsif !proven_by?(code_verifier)
      raise invalid("The code_verifier does not match the code_challenge")
    elsif resource.present? && resource != grant.resource
      raise OAuth::Error.new("invalid_target", "skyBlip issues tokens for #{grant.resource} alone")
    elsif (tokens = transaction { grant.issue_tokens if claimed? })
      tokens
    else
      reused
    end
  end

  private
    def proven_by?(verifier)
      verifier.to_s.match?(VERIFIER) &&
        ActiveSupport::SecurityUtils.secure_compare(self.class.challenge_of(verifier), code_challenge)
    end

    # INFO: fc 13oct26 concurrency: a code spends once, claim and tokens commit together, so a racing reuse revokes the winner's tokens too
    def claimed? = self.class.where(id:, redeemed_at: nil).update_all(redeemed_at: Time.current) == 1

    def reused
      grant.revoke
      raise invalid("The authorization code was used before, so the connection is revoked")
    end

    def invalid(description) = OAuth::Error.invalid_grant(description)
end
