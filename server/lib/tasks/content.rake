namespace :content do
  namespace :sitemap do
    desc "Generate the content sitemap, compressed copy, and robots.txt"
    task generate: :environment do
      sitemap = Content::Sitemap.new(generate_robots: true)
      puts "Generated public/sitemap.xml with #{sitemap.generate} content URLs"
    end

    desc "Check that the content sitemap and robots.txt are current"
    task check: :environment do
      sitemap = Content::Sitemap.new(generate_robots: true)
      abort "Content sitemap is out of date. Run bin/rails content:sitemap:generate." unless sitemap.current?

      puts "Content sitemap and robots.txt are current"
    end
  end

  namespace :share_images do
    desc "Render the 1200x630 link preview cards into content_images/share, in Chrome"
    task generate: :environment do
      ShareImages.new.generate.each { |path| puts path.relative_path_from(Rails.root) }
    end
  end
end
