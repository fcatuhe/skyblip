module Unit::Activating
  extend ActiveSupport::Concern

  # INFO: fc 18oct26 serial 0 would put 0x000000 on the air, which decoders read as no address
  SERIALS = 1..0xFFFF
  NEW_PER_NETWORK_PER_DAY = 5

  included do
    validates :serial, uniqueness: true, inclusion: { in: SERIALS }, allow_nil: true

    scope :activated, -> { where.not(serial: nil) }
    scope :activated_today, -> { where(activated_at: Time.current.all_day) }
  end

  class_methods do
    def register(device_id:, public_key:, ip_address: nil, **seen)
      transaction do
        unit = find_or_initialize_by(device_id:)
        unit.activate(network_of(ip_address)) unless unit.activated?
        unit.bind(public_key, **seen)
      end
    end

    def daily_cap = Activation::Cap.current.new_units_per_day

    def daily_cap_reached? = activated_today.count >= daily_cap

    def over_network_cap?(network) = network.present? && enrollments_from(network) > NEW_PER_NETWORK_PER_DAY

    # INFO: fc 14nov26 counted as Rails' rate_limit counts, in the cache for 24 hours from the network's first new unit, never in the database
    def enrollments_from(network) = Rails.cache.increment("unit-enrollments:#{network}", 1, expires_in: 24.hours).to_i

    # INFO: fc 08nov26 a home IPv6 line holds a whole /56, so a network is that /56, or one IPv4 address
    def network_of(ip_address)
      if ip_address.present?
        address = IPAddr.new(ip_address)
        address = address.native if address.ipv4_mapped?
        if address.private? || address.loopback? || address.link_local?
          Rails.logger.error "Activation: request from a private address #{address}, so it counts toward no network (#205)"
          nil
        else
          (address.ipv6? ? address.mask(56) : address).to_s
        end
      end
    end

    # INFO: fc 18oct26 SQLite transactions begin IMMEDIATE, so maximum and insert run under one write lock: serials stay dense
    def next_serial
      serial = (maximum(:serial) || 0) + 1
      if Unit::SERIALS.cover?(serial)
        serial
      else
        raise Activation::Refusal.new(:no_serial)
      end
    end
  end

  def activated? = serial.present?

  def activate(network)
    raise Activation::Refusal.new(:daily_cap) if self.class.daily_cap_reached?
    raise Activation::Refusal.new(:network_cap) if self.class.over_network_cap?(network)

    update!(serial: self.class.next_serial, activated_at: Time.current)
    Rails.event.notify "unit.enrolled", unit_id: id, serial:, device: slug
    Rails.logger.error "Activation: #{self.class.daily_cap} new units today, the daily cap, so new ones wait for midnight UTC" if self.class.daily_cap_reached?
  end
end
