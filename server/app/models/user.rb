class User < ApplicationRecord
  has_secure_password
  has_many :sessions, dependent: :destroy
  has_many :oauth_grants, class_name: "OAuth::Grant", dependent: :destroy

  enum :role, { user: 0, admin: 9 }

  validates :email, presence: true, uniqueness: true, format: { with: URI::MailTo::EMAIL_REGEXP }
  validates :password, length: { minimum: 12 }, allow_blank: true
  validates :first_name, :last_name, presence: true

  normalizes :email, with: -> { it.strip.downcase }

  after_update :end_other_sessions, if: :saved_change_to_password_digest?
  after_create_commit -> { Rails.event.notify "user.created", person_id: id, role:, by_id: Current.user&.id }
  after_update_commit -> { Rails.event.notify "user.role_changed", person_id: id, was: role_previously_was, role:, by_id: Current.user&.id },
    if: :saved_change_to_role?
  after_destroy_commit -> { Rails.event.notify "user.destroyed", person_id: id, by_id: Current.user&.id }

  def name = "#{first_name} #{last_name}"
  alias to_s name

  def can_administer?(other) = admin? && other != self

  private
    def end_other_sessions = sessions.excluding(Current.session).destroy_all
end
