class Briefing::Handoff
  PRODUCTS = %w[go bravo].freeze
  TAG = /\A\h{1,4}\z/
  BUILD = /\A\d{1,3}\.\d{1,3}\.\d{1,5}\+\d{1,10}\z/

  attr_reader :product, :tag, :build

  def self.from(params)
    if (product = params[:installed].to_s.presence_in(PRODUCTS))
      new(product:, tag: params[:tag].to_s[TAG]&.downcase, build: params[:build].to_s[BUILD])
    end
  end

  def initialize(product:, tag: nil, build: nil)
    @product = product
    @tag = tag
    @build = build
  end

  def to_query = { installed: product, tag:, build: }.compact

  def code(url) = RQRCode::QRCode.new(url).as_svg(module_size: 6, standalone: true, use_path: true, viewbox: true, color: "000", fill: "fff")
end
