class User < ApplicationRecord
  has_secure_password
  has_many :sessions, dependent: :destroy

  enum :role, { user: 0, admin: 9 }

  validates :email, presence: true, uniqueness: true, format: { with: URI::MailTo::EMAIL_REGEXP }
  validates :password, length: { minimum: 12 }, allow_blank: true
  validates :first_name, :last_name, presence: true

  normalizes :email, with: -> { it.strip.downcase }

  after_update :end_other_sessions, if: :saved_change_to_password_digest?

  def name = "#{first_name} #{last_name}"
  alias to_s name

  def can_administer?(other) = admin? && other != self

  private
    def end_other_sessions = sessions.excluding(Current.session).destroy_all
end
