Rails.application.routes.draw do
  constraints host: "www.#{URI(Rails.configuration.x.public_url).host}" do
    get "(*path)", format: false, to: redirect { |_params, request| "#{Rails.configuration.x.public_url}#{request.fullpath}" }
  end

  constraints ->(request) { request.original_fullpath.match?(%r{\A/[^?]+/(\?|\z)}) } do
    get "*path", format: false, to: redirect { |_params, request| request.original_fullpath.sub(%r{/+(\?|\z)}, '\\1') }
  end

  get "up" => "rails/health#show", as: :rails_health_check

  resource :mcp, only: %i[show create destroy]

  namespace :oauth do
    resource :authorization, only: %i[new create destroy]
    resource :token, only: :create
    resource :revocation, only: :create
  end

  scope ".well-known", module: :oauth do
    resource :authorization_server, only: :show, path: "oauth-authorization-server"
    resource :protected_resource, only: :show, path: "oauth-protected-resource/mcp"
  end

  get "/.well-known/*", to: ->(_env) { [ 204, {}, [] ] }

  mount Ahoy::Engine, at: :hoy

  resource :session, only: %i[new create destroy]
  resources :events, only: :create

  namespace :admin do
    resource :analytics, only: :show do
      scope module: :analytics do
        resources :visitors, only: %i[index show], param: :visitor_token
      end
    end
    resource :insights, only: :show
    resources :events, only: :index
    resources :errors, only: %i[index show], param: :fingerprint
    resources :agents, only: %i[index destroy]
    resources :users
    resources :units, only: %i[index show]
    resources :captures, only: %i[index show]
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

  namespace :attestation do
    resources :challenges, only: :create
    resources :statements, only: :create
  end

  resources :releases, only: [], param: :tag do
    resources :assets, only: :show, module: :releases, path: "", param: :file,
      format: false, constraints: { release_tag: /[^\/]+/, file: /[^\/]+/ }
  end

  # INFO: fc 11nov26 the glass prints skyblip.eu/activate, and the device pages lived on the public site before the briefing app | 13nov26 the briefing dropped its /fr addresses for a language setting | 13nov26 the Go's page moved from /features to /go | 13nov26 the ADS-L pages moved under /ads-l
  get "activate", to: redirect("/briefing/activate")
  get "fr/activer", to: redirect("/briefing/activate")
  %w[update configure manage].each { |path| get path, to: redirect("/briefing") }
  %w[mettre-a-jour regler mise-a-jour].each { |path| get "fr/#{path}", to: redirect("/briefing") }
  get "fr/briefing(/*path)", format: false, to: redirect { |_params, request| request.fullpath.delete_prefix("/fr") }
  get "features", to: redirect("/go")
  get "fr/fonctionnalites", to: redirect("/fr/go")
  get "ads-l-registry", to: redirect("/ads-l/registry")
  get "ads-l-registry/nmea", to: redirect("/ads-l/nmea")

  namespace :briefing do
    root "devices#index"
    resource :manifest, only: :show, defaults: { format: :json }
    resources :devices, only: :show, param: :slug, constraints: { slug: /(go|bravo)-[0-9a-f]+/ } do
      resource :firmware, only: :show, module: :devices
      resource :terminal, only: :show, module: :devices
      resources :captures, only: :create, module: :devices
    end
    resource :activation, only: :show, path: "activate"
    resource :preferences, only: %i[show update]
    resource :tools, only: :show
    resource :firmwares, only: :show
    resource :simulator_build, only: :show
    resource :prompt_glass, only: :show
    resource :simulator, only: :show
    resource :console, only: :show
    resource :comparison, only: :show
    resource :ownship, only: :show
  end

  scope "(:locale)", locale: /#{I18n.available_locales.without(I18n.default_locale).join("|")}/ do
    draw :content
  end
end
