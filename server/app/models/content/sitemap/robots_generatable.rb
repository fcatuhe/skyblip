module Content::Sitemap::RobotsGeneratable
  extend ActiveSupport::Concern

  private
    def write_robots_file
      robots_file_path.write(robots_content)
    end

    def robots_file_path
      Pathname.new(build_dir).join("robots.txt")
    end

    def robots_content
      "User-agent: *\nAllow: /\n\n" \
        "Sitemap: #{sitemap_url(compressed: true)}\nSitemap: #{sitemap_url}\n"
    end
end
