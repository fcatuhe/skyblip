module StructuredDataHelper
  def structured_data_tag
    if @page
      tag.script(json_escape({ "@context" => "https://schema.org", "@graph" => structured_data_graph }.to_json).html_safe, type: "application/ld+json")
    end
  end

  def structured_data_graph
    [ organization_data, website_data, web_page_data, breadcrumb_data, article_data, product_data ].compact
  end

  private
    def organization_data
      {
        "@type" => "Organization",
        "@id" => structured_data_id("organization"),
        "name" => site_name,
        "url" => Content.canonical_url("/"),
        "logo" => Content.canonical_url("/icon.png"),
        "sameAs" => [ Rails.configuration.x.repository_url ]
      }
    end

    def website_data
      {
        "@type" => "WebSite",
        "@id" => structured_data_id("website"),
        "name" => site_name,
        "url" => Content.canonical_url("/"),
        "inLanguage" => I18n.available_locales.map(&:to_s),
        "publisher" => { "@id" => structured_data_id("organization") }
      }
    end

    def web_page_data
      {
        "@type" => "WebPage",
        "@id" => canonical_url,
        "url" => canonical_url,
        "name" => page_title,
        "description" => description,
        "inLanguage" => @page.locale.to_s,
        "isPartOf" => { "@id" => structured_data_id("website") },
        "primaryImageOfPage" => share_image_url,
        "dateModified" => @page.updated_on.iso8601,
        "breadcrumb" => ({ "@id" => structured_data_id("breadcrumb", canonical_url) } if @page.parent)
      }.compact
    end

    def breadcrumb_data
      if @page.parent
        crumbs = [ Content::Page.find_by(base_slug: "index", locale: @page.locale), @page.parent, @page ]

        {
          "@type" => "BreadcrumbList",
          "@id" => structured_data_id("breadcrumb", canonical_url),
          "itemListElement" => crumbs.each_with_index.map do |page, index|
            { "@type" => "ListItem", "position" => index + 1, "name" => page.base_slug == "index" ? site_name : page.nav_label, "item" => page_url_for(page) }
          end
        }
      end
    end

    def article_data
      if @page.article?
        {
          "@type" => "Article",
          "headline" => page_title,
          "description" => description,
          "inLanguage" => @page.locale.to_s,
          "mainEntityOfPage" => { "@id" => canonical_url },
          "image" => share_image_url,
          "datePublished" => @page.published_on.iso8601,
          "dateModified" => @page.updated_on.iso8601,
          "author" => { "@type" => "Person", "name" => author },
          "publisher" => { "@id" => structured_data_id("organization") }
        }
      end
    end

    def product_data
      if (product = @page.product)
        {
          "@type" => "Product",
          "name" => product.fetch(:name),
          "description" => description,
          "image" => [ Content.canonical_url(image_path(product.fetch(:image))), share_image_url ],
          "url" => canonical_url,
          "brand" => { "@id" => structured_data_id("organization") },
          "offers" => {
            "@type" => "AggregateOffer",
            "lowPrice" => product.fetch(:low_price),
            "priceCurrency" => product.fetch(:currency),
            "availability" => "https://schema.org/InStock",
            "url" => page_url_for(Content::Page.find_by(base_slug: "buy", locale: @page.locale))
          }
        }
      end
    end

    def structured_data_id(name, base = Content.canonical_url("/"))
      "#{base}##{name}"
    end
end
