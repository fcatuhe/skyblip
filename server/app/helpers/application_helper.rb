module ApplicationHelper
  def render_erb(content)
    render inline: content, layout: false
  end

  def site_name
    "skyBlip"
  end

  def title
    if page_title.blank? || page_title.include?(site_name)
      page_title.presence || site_name
    else
      "#{page_title} · #{site_name}"
    end
  end

  def page_title
    @page ? @page.title : content_for(:title)
  end

  def description
    @page ? @page.description : content_for(:description)
  end

  def author
    "François Catuhe"
  end

  def canonical_url
    Content.canonical_url(@page ? @page.url_path : request.path)
  end

  def theme_color = Rails.configuration.x.theme_color

  def robots_content
    if Rails.env.production? && !content_for?(:noindex)
      "index, follow"
    else
      "noindex, nofollow"
    end
  end
end
