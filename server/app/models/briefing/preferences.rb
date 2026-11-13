class Briefing::Preferences
  include ActiveModel::Model
  include ActiveModel::Attributes

  attribute :locale, :string

  validates :locale, inclusion: { in: -> { locales } }

  def self.locales = I18n.available_locales.map(&:to_s)
end
