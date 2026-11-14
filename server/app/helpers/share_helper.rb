module ShareHelper
  def share_meta_tags
    properties = open_graph_properties.compact_blank.flat_map do |property, contents|
      Array(contents).map { |content| tag.meta(property:, content:) }
    end

    safe_join([ *properties, tag.meta(name: "twitter:card", content: "summary_large_image") ], "\n")
  end

  def share_image_url
    Content.canonical_url(image_path("share/#{ShareImages.filename(share_image_name)}"))
  end

  private
    def open_graph_properties
      {
        "og:type" => @page&.article? ? "article" : "website",
        "og:site_name" => site_name,
        "og:title" => page_title.presence || site_name,
        "og:description" => description,
        "og:url" => canonical_url,
        "og:locale" => t("share.locale"),
        "og:locale:alternate" => page_alternates.keys.without(I18n.locale).map { |locale| t("share.locale", locale:) },
        "og:image" => share_image_url,
        "og:image:type" => "image/jpeg",
        "og:image:width" => ShareImages::WIDTH,
        "og:image:height" => ShareImages::HEIGHT,
        "og:image:alt" => t("share.images.#{share_image_name}"),
        "article:published_time" => (@page.published_on.iso8601 if @page&.article?),
        "article:modified_time" => (@page.updated_on.iso8601 if @page&.article?)
      }
    end

    def share_image_name = @page&.share_image || "go"
end
