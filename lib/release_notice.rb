class ReleaseNotice
  def initialize(source)
    @source = source
  end

  def to_s
    [ File.read(File.join(@source, "NOTICE.md")).chomp, "## License texts", *texts.map { |path| text_section(path) } ].join("\n\n") + "\n"
  end

  private
    def texts
      licenses = Dir.glob("LICENSES/*.txt", base: @source).sort
      raise ArgumentError, "No license texts in #{@source}/LICENSES" if licenses.empty?

      [ "LICENSE", *licenses ]
    end

    def text_section(path) = "### #{path}\n\n```\n#{File.read(File.join(@source, path)).chomp}\n```"
end
