class Briefing::Preferences
  include ActiveModel::Model
  include ActiveModel::Attributes

  attribute :development_builds, :boolean

  def self.locales = I18n.available_locales.map(&:to_s)
end
