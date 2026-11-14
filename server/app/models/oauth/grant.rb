class OAuth::Grant < ApplicationRecord
  ACCESS_LIFETIME = 1.hour
  REFRESH_LIFETIME = 30.days
  APPROVAL_LIFETIME = 30.days
  USE_GRANULARITY = 1.minute
  LAPSE = 1.hour

  belongs_to :user
  belongs_to :client

  has_many :codes, dependent: :delete_all
  has_many :tokens, dependent: :delete_all

  scope :latest_first, -> { order(updated_at: :desc) }
  scope :lapsed, -> { where.missing(:tokens).where(updated_at: ...LAPSE.ago) }
  scope :expired, -> { where(approved_at: ...APPROVAL_LIFETIME.ago) }

  after_destroy_commit -> do
    Rails.event.notify "oauth_grant.ended", grant_id: id, client_id:, age_seconds: (Time.current - created_at).round,
      used: last_used_at?
  end

  def self.approve(user:, client:)
    find_or_initialize_by(user:, client:).tap do |grant|
      renewed = grant.persisted?
      grant.update! scope: OAuth::SCOPE, resource: OAuth.resource, approved_at: Time.current
      Rails.event.notify("oauth_grant.approved", grant_id: grant.id, client_id: client.id, renewed:)
    end
  end

  def issue_code(code_challenge:, redirect_uri:) = OAuth::Code.issue(self, code_challenge:, redirect_uri:)

  def issue_tokens
    issue_access.merge refresh_token: OAuth::Token.issue(self, :refresh, within_approval(REFRESH_LIFETIME.from_now))
  end

  def issue_access
    now = Time.current
    access_expiry = within_approval(now + ACCESS_LIFETIME)

    { access_token: OAuth::Token.issue(self, :access, access_expiry), token_type: "Bearer",
      expires_in: (access_expiry - now).to_i, scope: scope }
  end

  def used
    if last_used_at.nil? || last_used_at.before?(USE_GRANULARITY.ago)
      touch :last_used_at
    end
  end

  def revoke = destroy!

  private
    # INFO: fc 14nov26 security: an admin's word decides who reads everybody's events, so no token outlives the consent by more than a month
    def within_approval(expiry) = [ expiry, approved_at + APPROVAL_LIFETIME ].min
end
