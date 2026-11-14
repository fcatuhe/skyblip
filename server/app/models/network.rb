module Network
  extend self

  def table_name_prefix
    "network_"
  end

  # INFO: fc 08nov26 a home IPv6 line holds a whole /56, so a network is that /56, or one IPv4 address
  def of(ip_address)
    if ip_address.present?
      address = IPAddr.new(ip_address)
      address = address.native if address.ipv4_mapped?
      unless address.private? || address.loopback? || address.link_local?
        (address.ipv6? ? address.mask(56) : address).to_s
      end
    end
  end

  def pseudonym_of(ip_address)
    if (network = of(ip_address))
      Network::Salt.today.digest(network)
    end
  end

  def rate_limit_key(ip_address) = pseudonym_of(ip_address) || Network::Salt.today.digest(ip_address.to_s)
end
