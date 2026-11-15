class Unit::Firmware < ApplicationRecord
  SOURCES = %w[read installed attested].freeze

  belongs_to :unit

  enum :source, SOURCES.index_by(&:itself)
  enum :variant, Unit.variants, validate: { allow_nil: true }

  scope :newest_first, -> { order(created_at: :desc, id: :desc) }

  validates :version, format: { with: Activation::VERSION }
  validates :release, inclusion: { in: [ true, false ] }

  def continued_by?(version:, release:, variant: nil)
    self.version == version && self.release == release && (variant.nil? || self.variant.nil? || self.variant == variant)
  end

  # INFO: fc 15nov26 a stretch only gains trust: read, then installed from the briefing app, then attested by the unit
  def continue(source:, variant: nil, at: Time.current, **install)
    self.variant ||= variant
    self.source = source if SOURCES.index(source) > SOURCES.index(self.source)
    assign_attributes(install.compact)
    self.updated_at = [ updated_at, at ].max
    save!
    self
  end
end
