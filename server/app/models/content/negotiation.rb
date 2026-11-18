class Content::Negotiation
  def self.markdown?(request)
    request.path_parameters[:format] == "md" || new(request.accept).format == :md
  end

  def initialize(accept)
    @qualities = accept.to_s.split(",").filter_map do |range|
      type, *parameters = range.split(";").map(&:strip)
      quality = parameters.find { it.start_with?("q=") }&.delete_prefix("q=")

      [ type.downcase, quality ? quality.to_f : 1.0 ] if type.present?
    end.to_h
  end

  # INFO: fc 18nov26 coding agents send "text/markdown, text/html" at equal quality, so a tie goes to Markdown, but only when it is named
  def format
    if @qualities.empty?
      :html
    elsif markdown.positive? && markdown >= html
      :md
    elsif html.positive?
      :html
    end
  end

  private
    def markdown = @qualities.fetch("text/markdown", 0)

    def html = @qualities.values_at("text/html", "text/*", "*/*").compact.first || 0
end
