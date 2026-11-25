atom_feed id: Content.canonical_url(content_feed_path), root_url: Content.canonical_url(root_path),
  url: Content.canonical_url(content_feed_path), language: I18n.locale do |feed|
  feed.title "skyBlip"
  feed.subtitle t("footer.description")
  feed.updated @pages.first.updated_on.to_time(:utc)
  feed.author { it.name "skyBlip" }

  @pages.each do |page|
    feed.entry page, id: page.canonical_url, url: false,
      published: page.published_on&.to_time(:utc), updated: page.updated_on.to_time(:utc) do |entry|
      entry.link rel: "alternate", type: "text/html", href: page.canonical_url, hreflang: page.locale
      entry.link rel: "alternate", type: "text/markdown", href: page.markdown_url, hreflang: page.locale
      entry.title page.title
      entry.summary page.description
    end
  end
end
