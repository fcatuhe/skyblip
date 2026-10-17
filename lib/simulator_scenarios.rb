require "json"

class SimulatorScenarios
  def self.generate(directory)
    paths = Dir.glob(File.join(directory, "*.json")).sort
    raise ArgumentError, "No simulator scenarios in #{directory}" if paths.empty?

    JSON.generate(paths.to_h { |path| [ File.basename(path), JSON.parse(File.read(path)) ] })
  end
end
