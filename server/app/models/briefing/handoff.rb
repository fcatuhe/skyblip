class Briefing::Handoff
  PRODUCTS = %w[go bravo].freeze
  TAG = /\A\h{1,4}\z/

  attr_reader :product, :tag

  def self.from(params)
    if (product = params[:installed].to_s.presence_in(PRODUCTS))
      new(product:, tag: params[:tag].to_s[TAG]&.downcase)
    end
  end

  def initialize(product:, tag: nil)
    @product = product
    @tag = tag
  end

  def to_query = { installed: product, tag: }.compact

  def code(url) = RQRCode::QRCode.new(url).as_svg(module_size: 6, standalone: true, use_path: true, viewbox: true, color: "000", fill: "fff")
end
