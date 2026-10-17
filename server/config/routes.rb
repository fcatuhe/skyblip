Rails.application.routes.draw do
  get "up" => "rails/health#show", as: :rails_health_check
  get "/.well-known/*", to: ->(_env) { [ 204, {}, [] ] }

  resources :releases, only: [], param: :tag do
    resources :assets, only: :show, module: :releases, path: "", param: :file,
      format: false, constraints: { release_tag: /[^\/]+/, file: /[^\/]+/ }
  end

  scope "(:locale)", locale: /#{I18n.available_locales.without(I18n.default_locale).join("|")}/ do
    namespace :briefing do
      root "dashboards#show"
      resource :firmwares, only: :show
      resource :simulator_build, only: :show
      resource :simulator, only: :show
      resource :console, only: :show
    end

    draw :content
  end
end
