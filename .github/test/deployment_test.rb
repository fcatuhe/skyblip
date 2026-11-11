require "minitest/autorun"
require "erb"
require "yaml"
require "json"
require "open3"
require "tmpdir"
require "fileutils"

class DeploymentTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_kamal_preserves_the_clean_clone_and_server_directory
    builder = deployment.fetch("builder")

    assert_equal "arm64", builder.fetch("arch")
    refute builder.key?("context")
    refute builder.key?("dockerfile")
  end

  def test_application_runs_as_debian_and_the_remote_builder_on_the_crooz_ssh_host
    config = deployment
    host = ENV.fetch("SKYBLIP_SERVER_IP", "crooz")

    assert_equal [ host ], config.fetch("servers").fetch("web")
    assert_equal "debian", config.fetch("ssh").fetch("user")
    assert_equal "localhost:5555", config.fetch("registry").fetch("server")
    if RUBY_PLATFORM.match?(/aarch64|arm64/)
      refute config.fetch("builder").key?("remote")
    else
      assert_equal "ssh://crooz", config.fetch("builder").fetch("remote")
    end
  end

  def test_sqlite_and_solid_queue_live_in_the_persistent_volume
    assert_equal [ "skyblip_storage:/rails/storage" ], deployment.fetch("volumes")
    assert deployment.fetch("env").fetch("clear").fetch("SOLID_QUEUE_IN_PUMA")
    assert_equal [ "RAILS_MASTER_KEY" ], deployment.fetch("env").fetch("secret")
  end

  def test_deployment_waits_for_the_gates_on_main
    workflow = YAML.load_file(File.join(ROOT, ".github/workflows/server.yml"))
    jobs = workflow.fetch("jobs")
    deploy = jobs.fetch("deploy")

    assert_equal (jobs.keys - %w[deploy]).sort, deploy.fetch("needs").sort
    assert_includes deploy.fetch("if"), "github.ref == 'refs/heads/main'"
    refute deploy.fetch("concurrency").fetch("cancel-in-progress")
    assert_equal "server_production", deploy.fetch("environment").fetch("name")
  end

  def test_production_checksums_the_simulator_before_publishing
    workflow = File.read(File.join(ROOT, ".github/workflows/release.yml"))
    copy = workflow.index(%r{^ +cp simulator/skyblip_simulator\.js})
    checksums = workflow.index("sha256sum -- * > SHA256SUMS")

    assert copy, "release copies simulator release assets"
    assert checksums, "release generates checksums"
    assert_operator copy, :<, checksums
    assert_includes workflow, "/embed.js release/"
    scenarios = workflow.index("cp simulator/scenarios.json release/")
    assert scenarios, "release includes the scenario bundle"
    assert_operator scenarios, :<, checksums
    assert_includes workflow, "subject-checksums: release/SHA256SUMS"
  end

  def test_every_production_image_ships_its_third_party_notices
    workflow = File.read(File.join(ROOT, ".github/workflows/release.yml"))
    notices = workflow.index(%r{^ +cp LICENSE NOTICE\.md LICENSES/\*\.txt release/$})
    checksums = workflow.index("sha256sum -- * > SHA256SUMS")

    assert notices, "release copies LICENSE, NOTICE.md and the license texts into the release"
    assert_operator notices, :<, checksums
  end

  # What the development pre-release stages is lib/dev_release.rb's, tested in dev_release_test.rb.
  def test_development_publishes_every_product_through_the_commits_own_dev_release
    job = YAML.load_file(File.join(ROOT, ".github/workflows/firmware.yml")).fetch("jobs").fetch("dev-release")
    steps = job.fetch("steps")
    checkout = steps.find { |step| step["uses"].to_s.start_with?("actions/checkout") }

    assert steps.any? { |step| step.dig("with", "pattern") == "skyblip-*" && step.dig("with", "merge-multiple") }
    assert_equal %w[LICENSE NOTICE.md LICENSES lib/dev_release.rb scripts/dev_release.rb], checkout.dig("with", "sparse-checkout").split
    assert_equal "ruby source/scripts/dev_release.rb image simulator source", steps.last.fetch("run")
  end

  def test_each_release_tag_names_its_product_and_its_version
    Dir.mktmpdir do |tree|
      FileUtils.mkdir_p(File.join(tree, "firmware/products/skyblip_go"))
      FileUtils.mkdir_p(File.join(tree, "firmware/products/skyblip_bravo"))

      assert_equal({ "product" => "skyblip_go", "prefix" => "", "slug" => "skyblip-go", "version" => "0.1.0" }, tag_outputs(tree, "v0.1.0"))
      assert_equal({ "product" => "skyblip_bravo", "prefix" => "bravo-", "slug" => "skyblip-bravo", "version" => "0.2.0" }, tag_outputs(tree, "bravo-v0.2.0"))
      assert_equal %w[v* bravo-v*], workflow("release").fetch(true).fetch("push").fetch("tags")
    end
  end

  def test_a_tag_naming_no_product_in_the_tree_is_refused_before_the_approval
    Dir.mktmpdir do |tree|
      FileUtils.mkdir_p(File.join(tree, "firmware/products/skyblip_go"))
      output, status = run_step(tree, release_step("tag", "the tag names a product"), "GITHUB_REF_NAME" => "bravo-v0.1.0")

      refute status.success?
      assert_includes output, "FAIL: bravo-v0.1.0 names skyblip_bravo, which is not in firmware/products/"
      assert_includes workflow("release").fetch("jobs").fetch("image").fetch("needs"), "tag"
    end
  end

  def test_the_tag_must_equal_its_products_version
    Dir.mktmpdir do |tree|
      FileUtils.mkdir_p(File.join(tree, "firmware/products/skyblip_bravo"))
      File.write(File.join(tree, "firmware/products/skyblip_bravo/VERSION"), "VERSION_MAJOR = 0\nVERSION_MINOR = 2\nPATCHLEVEL = 0\nVERSION_TWEAK = 0\n")
      step = release_step("tag", "the tag is the product's VERSION")

      assert run_step(tree, step, "GITHUB_REF_NAME" => "bravo-v0.2.0", "PRODUCT" => "skyblip_bravo", "RELEASE_VERSION" => "0.2.0").last.success?
      output, status = run_step(tree, step, "GITHUB_REF_NAME" => "bravo-v0.2.1", "PRODUCT" => "skyblip_bravo", "RELEASE_VERSION" => "0.2.1")
      refute status.success?
      assert_includes output, "FAIL: tag bravo-v0.2.1, but firmware/products/skyblip_bravo/VERSION says 0.2.0"
    end
  end

  def test_a_drafts_notes_start_at_its_own_products_last_release
    Dir.mktmpdir do |tree|
      fake_gh(tree, %w[bravo-v0.2.0 v0.1.0])
      draft = ->(tag, prefix) { run_step(tree, release_step("draft", "create the draft"), "PATH" => "#{tree}:#{ENV.fetch("PATH")}", "GITHUB_REF_NAME" => tag, "GITHUB_REPOSITORY" => "fcatuhe/skyblip", "PREFIX" => prefix) }

      assert_includes draft.("v0.2.0", "").first, "--generate-notes --notes-start-tag v0.1.0"
      assert_includes draft.("bravo-v0.3.0", "bravo-").first, "--generate-notes --notes-start-tag bravo-v0.2.0"

      fake_gh(tree, %w[v0.1.0])
      output, status = draft.("bravo-v0.1.0", "bravo-")
      assert status.success?, output
      assert_includes output, "gh release create bravo-v0.1.0"
      refute_includes output, "--notes-start-tag"
    end
  end

  # The Go's release files keep the names they had before a second product.
  def test_a_release_names_its_files_after_the_product_and_the_version
    Dir.mktmpdir do |tree|
      release_tree(tree, slug: "skyblip-go", hub_image: true)
      assert run_step(tree, release_step("image", "name the files after the release"), "SLUG" => "skyblip-go", "RELEASE_VERSION" => "0.1.0").last.success?
      assert_equal %w[skyblip-go-0.1.0.full.signed.bin skyblip-go-0.1.0.signed.bin skyblip-go-0.1.0.uf2], Dir.children(File.join(tree, "release")).grep(/\Askyblip-/).sort
    end

    Dir.mktmpdir do |tree|
      release_tree(tree, slug: "skyblip-bravo", hub_image: false)
      assert run_step(tree, release_step("image", "name the files after the release"), "SLUG" => "skyblip-bravo", "RELEASE_VERSION" => "0.2.0").last.success?
      assert_equal %w[skyblip-bravo-0.2.0.signed.bin skyblip-bravo-0.2.0.uf2], Dir.children(File.join(tree, "release")).grep(/\Askyblip-/).sort
      assert_includes File.read(File.join(tree, "release/SHA256SUMS")), "  skyblip-bravo-0.2.0.uf2\n"
    end
  end

  def test_both_simulator_artifacts_contain_the_scenario_bundle
    %w[firmware release].each do |name|
      workflow = YAML.load_file(File.join(ROOT, ".github/workflows/#{name}.yml"))
      steps = workflow.fetch("jobs").fetch("simulator").fetch("steps")
      artifact = steps.find { |step| step.dig("with", "name") == "browser-simulator" }

      assert_includes artifact.fetch("with").fetch("path").lines.map(&:strip), "firmware/build/browser/scenarios.json", name
      assert steps.any? { |step| step["run"]&.include?("ruby scripts/simulator_scenarios.rb > firmware/build/browser/scenarios.json") }, name
    end
  end

  def test_the_job_holding_the_production_key_runs_no_emscripten
    image = YAML.load_file(File.join(ROOT, ".github/workflows/release.yml")).fetch("jobs").fetch("image")

    assert_includes image.fetch("needs"), "simulator"
    refute image.fetch("steps").any? { |step| step["uses"].to_s.include?("setup-emsdk") || step["run"].to_s.include?("make -C firmware browser") }
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

    def release_step(job, name) = workflow("release").fetch("jobs").fetch(job).fetch("steps").find { |step| step["name"] == name }.fetch("run")

    def run_step(tree, script, env)
      output, status = Open3.capture2e(env, "bash", "-e", "-o", "pipefail", "-c", script, chdir: tree)
      [ output, status ]
    end

    def fake_gh(tree, published)
      File.write(File.join(tree, "gh"), <<~SH)
        #!/usr/bin/env bash
        if [ "$1 $2" = "release list" ]; then printf '%s\n' #{published.join(" ")}; else echo "gh $*"; fi
      SH
      File.chmod(0o755, File.join(tree, "gh"))
    end

    def tag_outputs(tree, tag)
      outputs = File.join(tree, "outputs")
      File.write(outputs, "")
      output, status = run_step(tree, release_step("tag", "the tag names a product"), "GITHUB_REF_NAME" => tag, "GITHUB_OUTPUT" => outputs)
      assert status.success?, output
      File.readlines(outputs, chomp: true).to_h { |line| line.split("=", 2) }
    end

    def release_tree(tree, slug:, hub_image:)
      %w[builds simulator LICENSES].each { |dir| FileUtils.mkdir_p(File.join(tree, dir)) }
      endings = %w[.uf2 .signed.bin .signed.confirmed.bin .version] + (hub_image ? %w[.full.signed.bin] : [])
      endings.each { |ending| File.write(File.join(tree, "builds", "#{slug}#{ending}"), ending) }
      %w[skyblip_simulator.js skyblip_simulator.wasm embed.js scenarios.json].each { |file| File.write(File.join(tree, "simulator", file), file) }
      %w[LICENSE NOTICE.md LICENSES/Apache-2.0.txt].each { |file| File.write(File.join(tree, file), file) }
    end

    def deployment
      source = File.read(File.join(ROOT, "server/config/deploy.yml"))
      YAML.safe_load(ERB.new(source).result)
    end
end
