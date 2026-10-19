class Identity::ServerKey
  attr_reader :id, :public_key

  def self.current
    seed = Rails.configuration.x.identity.server_private_key.presence || raise(KeyError, "no identity.server_private_key in the credentials")
    (@keys ||= {})[seed] ||= new(seed)
  end

  def self.configured? = Rails.configuration.x.identity.server_private_key.present?

  def self.named(id)
    current if current.id == id
  end

  def initialize(seed)
    @private_key = OpenSSL::PKey.new_raw_private_key("ED25519", [ seed ].pack("H*"))
    @public_key = @private_key.raw_public_key.unpack1("H*")
    @id = OpenSSL::Digest::SHA256.digest(@private_key.raw_public_key).first(4).unpack1("H*")
  end

  def signature_for(message) = @private_key.sign(nil, message).unpack1("H*")
end
