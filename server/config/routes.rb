Rails.application.routes.draw do
  constraints host: "www.#{URI(Rails.configuration.x.public_url).host}" do
    get "(*path)", format: false, to: redirect { |_params, request| "#{Rails.configuration.x.public_url}#{request.fullpath}" }
  end

  get "up" => "rails/health#show", as: :rails_health_check
  get "/.well-known/*", to: ->(_env) { [ 204, {}, [] ] }

  mount Ahoy::Engine, at: :hoy

  resource :session, only: %i[new create destroy]

  namespace :admin do
    resource :analytics, only: :show do
      scope module: :analytics do
        resources :visitors, only: %i[index show], param: :visitor_token
      end
    end
    resources :users
    resources :units, only: %i[index show]
    resources :unit_keys, only: [] do
      scope module: :unit_keys do
        resource :promotion, only: :create
        resource :revocation, only: :create
      end
    end
    mount MissionControl::Jobs::Engine, at: :jobs
  end

  namespace :activation do
    resources :challenges, only: :create
    resources :registrations, only: :create
  end

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
