class Briefing::Preferences
  include ActiveModel::Model
  include ActiveModel::Attributes

  attribute :locale, :string
  attribute :development_builds, :boolean

  validates :locale, inclusion: { in: -> { locales } }

  def self.locales = I18n.available_locales.map(&:to_s)
end
