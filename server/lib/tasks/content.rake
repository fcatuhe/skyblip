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
end
