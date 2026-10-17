require "builder"
require "fileutils"
require "zlib"

class Content::Sitemap
  include ActiveModel::Model
  include ActiveModel::Attributes
  include Content::Sitemap::RobotsGeneratable

  attribute :base_url, :string, default: -> { Rails.application.config.x.public_url }
  attribute :build_dir, :string, default: -> { Rails.public_path.to_s }
  attribute :generate_robots, :boolean, default: false

  validates :base_url, presence: true, format: { with: /\Ahttps?:\/\/[^\s\/]+(?:\/[^\s]*)?\z/ }
  validates :build_dir, presence: true

  def generate
    raise ActiveModel::ValidationError, self unless valid?

    FileUtils.mkdir_p(build_dir)
    File.write(sitemap_path, to_xml)
    Zlib::GzipWriter.open(sitemap_path(compressed: true)) do |gzip|
      gzip.mtime = 0
      gzip.write(to_xml)
    end
    write_robots_file if generate_robots

    sitemap_entries.size
  end

  def current?
    sitemap_path.file? && sitemap_path.read == to_xml &&
      sitemap_path(compressed: true).file? &&
      Zlib::GzipReader.open(sitemap_path(compressed: true), &:read) == to_xml &&
      (!generate_robots || (robots_file_path.file? && robots_file_path.read == robots_content))
  end

  def to_xml
    xml = Builder::XmlMarkup.new(indent: 2)
    xml.instruct! :xml, version: "1.0", encoding: "UTF-8"
    xml.urlset(xmlns: "http://www.sitemaps.org/schemas/sitemap/0.9") do
      sitemap_entries.each do |entry|
        xml.url do
          xml.loc entry.canonical_url
          xml.changefreq entry.change_frequency
          xml.priority entry.priority
        end
      end
    end
  end

  private
    def sitemap_entries
      Content::Page.all
        .map { |page| Content::Sitemap::Entry.new(page, base_url:) }
        .sort_by(&:canonical_url)
    end

    def sitemap_path(compressed: false)
      Pathname.new(build_dir).join(sitemap_filename(compressed:))
    end

    def sitemap_filename(compressed: false)
      compressed ? "sitemap.xml.gz" : "sitemap.xml"
    end

    def sitemap_url(compressed: false)
      Content.canonical_url("/#{sitemap_filename(compressed:)}", host: base_url)
    end
end
