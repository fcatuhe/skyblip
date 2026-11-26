require "minitest/autorun"
require "yaml"
require "json"
require "open3"
require "tmpdir"
require "fileutils"
require_relative "../../lib/radio_protocols"

class DeploymentTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_deployment_waits_for_the_gates_on_main
    workflow = YAML.load_file(File.join(ROOT, ".github/workflows/server.yml"))
    jobs = workflow.fetch("jobs")
    deploy = jobs.fetch("deploy")

    assert_equal (jobs.keys - %w[deploy passed]).sort, deploy.fetch("needs").sort
    assert_includes deploy.fetch("if"), "github.ref == 'refs/heads/main'"
    refute deploy.fetch("concurrency").fetch("cancel-in-progress")
    assert_equal "server_production", deploy.fetch("environment").fetch("name")
  end

  def test_the_radio_protocols_notes_refuse_a_header_without_one_specification
    Dir.mktmpdir do |firmware|
      FileUtils.mkdir_p(File.join(firmware, "products/skyblip_bravo"))
      File.write(File.join(firmware, "products/skyblip_bravo/PROTOCOLS"), "adsb\n")
      FileUtils.mkdir_p(File.join(firmware, "protocols/adsb"))
      File.write(File.join(firmware, "protocols/adsb/version.h"), "inline constexpr char kVersion[] = \"0.1.0\";\n")
      error = assert_raises(ArgumentError) { RadioProtocols.new(firmware).notes(version: "0.2.0", products: %w[skyblip_bravo]) }
      assert_equal "No single kSpecification in protocols/adsb/version.h", error.message
    end
  end

  def test_the_jobs_holding_the_production_key_run_no_emscripten_and_write_nothing
    jobs = workflow("release").fetch("jobs")
    image = jobs.fetch("image")

    assert_includes image.fetch("needs"), "simulator"
    refute image.fetch("steps").any? { |step| step["uses"].to_s.include?("setup-emsdk") || step["run"].to_s.include?("make -C firmware browser") }
    refute image.key?("permissions")
    assert_equal({ "contents" => "read" }, workflow("release").fetch("permissions"))
    assert_equal [ "draft" ], jobs.select { |_name, job| job.dig("permissions", "contents") == "write" }.keys
  end

  # A moved tag runs new code in a job that can deploy, sign or write a release.
  def test_every_action_of_a_job_that_holds_a_secret_is_pinned_by_commit
    %w[firmware release server].each do |name|
      jobs = workflow(name).fetch("jobs").select { |_job_name, job| holds_a_secret?(job) }

      refute_empty jobs, name
      jobs.each do |job_name, job|
        job.fetch("steps").filter_map { it["uses"] }.each do |action|
          assert_match(/\A[\w.-]+\/[\w.\/-]+@\h{40}\z/, action, "#{name}.yml #{job_name}")
        end
      end
    end
  end

  def test_every_emsdk_setup_installs_the_version_the_pins_hold
    %w[firmware release server].each do |name|
      steps = workflow(name).fetch("jobs").values.flat_map { |job| job.fetch("steps", []) }
      setups = steps.select { |step| step["uses"].to_s.include?("setup-emsdk") }

      refute_empty setups, name
      setups.each { |step| assert_equal "${{ steps.emsdk.outputs.version }}", step.dig("with", "version"), name }
    end
  end

  def test_scenario_bundle_contains_the_firmware_scenarios_from_any_directory
    output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), chdir: Dir.tmpdir)
    paths = Dir.glob(File.join(ROOT, "firmware/scenarios/*.json")).sort

    assert status.success?, error
    scenarios = JSON.parse(output)
    assert_equal paths.map { |path| File.basename(path) }, scenarios.keys
    paths.each { |path| assert_equal JSON.parse(File.read(path)), scenarios.fetch(File.basename(path)) }
  end

  def test_scenario_bundle_accepts_a_source_directory_and_rejects_malformed_input
    Dir.mktmpdir do |directory|
      File.write(File.join(directory, "custom.json"), JSON.generate({ name: "custom", events: [] }))
      output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), directory)

      assert status.success?, error
      assert_equal({ "custom.json" => { "name" => "custom", "events" => [] } }, JSON.parse(output))

      File.write(File.join(directory, "broken.json"), "{")
      output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), directory)

      refute status.success?
      assert_empty output
      assert_includes error, "JSON::ParserError"
    end
  end

  def test_scenario_bundle_refuses_an_empty_directory
    Dir.mktmpdir do |directory|
      output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), directory)

      refute status.success?
      assert_empty output
      assert_includes error, "No simulator scenarios"
    end
  end

  private
    def workflow(name) = YAML.load_file(File.join(ROOT, ".github/workflows/#{name}.yml"))

    def holds_a_secret?(job)
      job.key?("environment") || job.to_s.include?("secrets.") || job.fetch("permissions", {}).value?("write")
    end

end
