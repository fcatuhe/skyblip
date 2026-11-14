module NetworkPseudonym
  private
    def network_pseudonym = Network.rate_limit_key(request.remote_ip)
end
