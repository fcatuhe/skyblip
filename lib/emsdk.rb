require "open3"

class Emsdk
  REPOSITORY = "https://github.com/emscripten-core/emsdk.git".freeze

  def self.emcc(candidate:, version:, directory:, repository: REPOSITORY, log: $stderr)
    new(version:, directory:, repository:, log:).emcc_for(candidate)
  end

  def initialize(version:, directory:, repository:, log:)
    @version = version
    @directory = directory
    @repository = repository
    @log = log
  end

  def emcc_for(candidate)
    if version_of(candidate) == @version
      candidate
    elsif version_of(installed_emcc) == @version
      installed_emcc
    else
      install
      installed = version_of(installed_emcc)
      if installed == @version
        installed_emcc
      else
        raise "#{@directory} runs Emscripten #{installed.inspect} after installing #{@version}"
      end
    end
  end

  private
    def installed_emcc = File.join(@directory, "upstream", "emscripten", "emcc")

    # INFO: fc 15nov26 nil means no runnable emcc there, which is what an install fixes
    def version_of(emcc)
      output, status = Open3.capture2e(emcc, "--version")
      output[/ (\d+\.\d+\.\d+) \(/, 1] if status.success?
    rescue Errno::ENOENT
      nil
    end

    def install
      @log.puts "emsdk: the pin is Emscripten #{@version}, installing it in #{@directory}"
      if File.exist?(File.join(@directory, "emsdk"))
        run("git", "-C", @directory, "pull", "--ff-only")
      else
        run("git", "clone", @repository, @directory)
      end
      run("./emsdk", "install", @version, chdir: @directory)
      run("./emsdk", "activate", @version, chdir: @directory)
    end

    def run(*command, chdir: Dir.pwd)
      @log.puts "emsdk: #{command.join(" ")}"
      system(*command, chdir:, out: @log, err: @log, exception: true)
    end
end
