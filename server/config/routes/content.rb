scope module: :content do
  root "pages#show", slug: "index"

  get "/*slug", to: "pages#show", as: :page, format: /md/,
    constraints: ->(request) {
      slug = request.path_parameters[:slug]
      locale = request.path_parameters[:locale] || I18n.default_locale

      slug.split("/").first != "briefing" &&
        (Content::Page.routable?(slug, locale:) || Content::Negotiation.markdown?(request))
    }
end
