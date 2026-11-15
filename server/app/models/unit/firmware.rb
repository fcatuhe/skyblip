class Unit::Firmware < ApplicationRecord
  SOURCES = %w[read installed attested].freeze

  belongs_to :unit

  enum :source, SOURCES.index_by(&:itself)
  enum :variant, Unit.variants, validate: { allow_nil: true }

  scope :newest_first, -> { order(created_at: :desc, id: :desc) }

  validates :version, format: { with: Activation::VERSION }
  validates :release, inclusion: { in: [ true, false ] }

  # INFO: fc 15nov26 a statement that names no variant must not vouch for one a less trusted source filled in
  def continued_by?(version:, release:, source:, variant: nil)
    if self.version == version && self.release == release
      if variant.nil?
        self.variant.nil? || !outranks?(source)
      else
        self.variant.nil? || self.variant == variant
      end
    end
  end

  # INFO: fc 15nov26 a stretch only gains trust: read, then installed from the briefing app, then attested by the unit
  def continue(source:, variant: nil, at: Time.current, **install)
    self.variant ||= variant unless outranked_by?(source)
    self.source = source if outranks?(source)
    assign_attributes(install.compact)
    self.updated_at = [ updated_at, at ].max
    save!
    self
  end

  private
    def outranks?(other) = SOURCES.index(other) > SOURCES.index(source)

    def outranked_by?(other) = SOURCES.index(other) < SOURCES.index(source)
end
