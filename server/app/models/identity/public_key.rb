module Identity::PublicKey
  extend self

  P = 2**255 - 19
  # INFO: fc 08nov26 a small-order key verifies signatures nobody made, so no unit may bind one or a non-canonical alias of one
  SMALL_ORDER_YS = [ 0, 1, P - 1,
    2707385501144840649318225287225658788936804267575313519463743609750303402022,
    55188659117513257062467267217118295137698188065244968500265048394206261417927 ]

  def weak?(hex)
    y = [ hex ].pack("H*").reverse.unpack1("H*").to_i(16) & (2**255 - 1)
    y >= P || SMALL_ORDER_YS.include?(y)
  end
end
