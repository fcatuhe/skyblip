class Activation::ServerKey
  attr_reader :name, :id, :public_key

  def self.all
    Rails.configuration.x.activation.server_keys.map { |name, seed| new(name, seed) }
  end

  def self.find(id)
    all.find { it.id == id } || raise(Activation::Refusal.new(:unknown_server_key))
  end

  def initialize(name, seed)
    @name = name.to_s
    @private_key = OpenSSL::PKey.new_raw_private_key("ED25519", [ seed ].pack("H*"))
    @public_key = @private_key.raw_public_key.unpack1("H*")
    @id = OpenSSL::Digest::SHA256.digest(@private_key.raw_public_key).first(4).unpack1("H*")
  end

  def signature_for(message) = @private_key.sign(nil, message).unpack1("H*")
end
