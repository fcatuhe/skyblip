module StylesheetsHelper
  def site_stylesheet_link_tags
    stylesheet_link_tag(*stylesheets_in("."), "data-turbo-track": "reload")
  end

  def area_stylesheet_link_tags(*areas)
    stylesheet_link_tag(*areas.flat_map { |area| stylesheets_in(area) }, "data-turbo-track": "dynamic")
  end

  private
    def stylesheets_in(directory)
      app_stylesheets_paths.select { |path| File.dirname(path) == directory }.presence ||
        raise(ArgumentError, "No stylesheets in app/assets/stylesheets/#{directory}")
    end
end
