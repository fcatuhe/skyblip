class Unit::Report
  include ActiveModel::API
  include ActiveModel::Attributes

  LONGEST_INSTALL = 1.day

  attribute :device_id, :string
  attribute :product, :string
  attribute :variant, :string
  attribute :version, :string
  attribute :release
  attribute :bluetooth_tag
  attribute :from, :string
  attribute :to, :string
  attribute :lasted_ms

  validates :device_id, format: { with: Activation::DEVICE_ID }
  validates :product, inclusion: { in: Unit.products.keys }, on: :read
  validates :variant, inclusion: { in: Unit.variants.keys }, on: :read
  validates :version, format: { with: Activation::VERSION }, on: :read
  validates :release, inclusion: { in: [ true, false ] }, on: :read
  validates :bluetooth_tag, inclusion: { in: Unit::BLUETOOTH_TAGS }, on: :read
  validates :to, format: { with: Activation::VERSION }, on: :install
  validates :from, format: { with: Activation::VERSION }, allow_nil: true, on: :install
  validates :lasted_ms, numericality: { only_integer: true, in: 0..LONGEST_INSTALL.in_milliseconds }, allow_nil: true, on: :install
end
