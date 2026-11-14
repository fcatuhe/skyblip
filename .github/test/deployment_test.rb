require "minitest/autorun"
require "erb"
require "yaml"
require "json"
require "open3"
require "tmpdir"
require "fileutils"
require_relative "../../lib/radio_protocols"
require_relative "../../server/app/models/release"

class DeploymentTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)
  RELEASE_ENV = { "PRODUCTS" => '["skyblip_bravo","skyblip_go"]', "RELEASE_VERSION" => "0.2.0" }.freeze

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
    assert_equal %w[LICENSE NOTICE.md LICENSES firmware/protocols/*/version.h firmware/products/*/PROTOCOLS lib/dev_release.rb lib/radio_protocols.rb scripts/dev_release.rb],
                 checkout.dig("with", "sparse-checkout").split
    assert_equal "ruby source/scripts/dev_release.rb image simulator source", steps.last.fetch("run")
  end

  def test_the_tag_must_equal_the_firmware_version
    Dir.mktmpdir do |tree|
      FileUtils.mkdir_p(File.join(tree, "firmware"))
      File.write(File.join(tree, "firmware/VERSION"), "# a comment\nVERSION_MAJOR = 0\nVERSION_MINOR = 2\nPATCHLEVEL = 0\nVERSION_TWEAK = 0\n")
      step = release_step("tag", "the tag is firmware/VERSION")

      assert_equal({ "version" => "0.2.0" }, step_outputs(tree, step, "GITHUB_REF_NAME" => "v0.2.0"))
      output, status = run_step(tree, step, "GITHUB_REF_NAME" => "v0.2.1", "GITHUB_OUTPUT" => File::NULL)
      refute status.success?
      assert_includes output, "FAIL: tag v0.2.1, but firmware/VERSION says 0.2.0"
      assert_equal %w[v*], workflow("release").fetch(true).fetch("push").fetch("tags")
    end
  end

  def test_every_product_that_declares_a_board_is_built_and_released
    Dir.mktmpdir do |tree|
      { "skyblip_go" => "skyblip_product_board(t_echo_plus)\n", "skyblip_bravo" => "skyblip_product_board(meshtracker_x1)\n", "common" => "" }.each do |product, board|
        FileUtils.mkdir_p(File.join(tree, "firmware/products", product))
        File.write(File.join(tree, "firmware/products", product, "CMakeLists.txt"), "project(#{product})\n#{board}")
      end

      assert_equal({ "products" => '["skyblip_bravo","skyblip_go"]' }, step_outputs(tree, release_step("tag", "every product in the tree")))
      assert_equal "${{ fromJSON(needs.tag.outputs.products) }}", workflow("release").dig("jobs", "image", "strategy", "matrix", "product")
    end
  end

  def test_a_tree_with_no_product_is_refused_before_the_approval
    Dir.mktmpdir do |tree|
      FileUtils.mkdir_p(File.join(tree, "firmware/products/common"))
      File.write(File.join(tree, "firmware/products/common/CMakeLists.txt"), "project(common)\n")
      output, status = run_step(tree, release_step("tag", "every product in the tree"), "GITHUB_OUTPUT" => File::NULL)

      refute status.success?
      assert_includes output, "FAIL: no directory in firmware/products/ declares a board"
      assert_includes workflow("release").fetch("jobs").fetch("image").fetch("needs"), "tag"
    end
  end

  # Zephyr reads products/<product>/VERSION, and a copy there would sign one version and report another.
  def test_every_product_carries_the_firmware_version
    assert_equal %w[skyblip_bravo skyblip_go], declared_products
    declared_products.each { |product| assert_equal "../../VERSION", File.readlink(File.join(ROOT, "firmware/products", product, "VERSION")), product }
  end

  # release.yml builds every product that declares a board, so each must also be built on main, served by the server and named in its release.
  def test_every_product_is_built_on_main_served_and_lists_its_radio_protocols
    matrix = workflow("firmware").dig("jobs", "product-image", "strategy", "matrix", "include")

    declared_products.each do |product|
      slug = product.tr("_", "-")
      assert File.file?(File.join(ROOT, "firmware/products", product, "PROTOCOLS")), "#{product} has no PROTOCOLS"
      assert_equal product, Release.product_of("#{slug}-0.1.0.signed.bin"), "Release::FIRMWARE does not match #{slug}"
      assert_includes matrix.map { |leg| [ leg.fetch("product"), leg.fetch("slug") ] }, [ product, slug ]
    end
  end

  # Before one release carried every product, Bravo's were tagged bravo-v*.
  def test_a_drafts_notes_start_at_the_last_published_release
    Dir.mktmpdir do |tree|
      fake_gh(tree, %w[bravo-v0.2.0 v0.1.0])
      draft = ->(tag) { run_step(tree, release_step("draft", "create the draft"), "PATH" => "#{tree}:#{ENV.fetch("PATH")}", "GITHUB_REF_NAME" => tag, "GITHUB_REPOSITORY" => "fcatuhe/skyblip") }

      assert_includes draft.("v0.2.0").first, "--generate-notes --notes-start-tag v0.1.0"

      fake_gh(tree, %w[bravo-v0.2.0])
      output, status = draft.("v0.1.0")
      assert status.success?, output
      assert_includes output, "gh release create v0.1.0"
      refute_includes output, "--notes-start-tag"
    end
  end

  def test_a_release_names_each_products_files_after_the_product_and_the_version
    Dir.mktmpdir do |tree|
      release_tree(tree, slug: "skyblip-go", hub_image: true)
      assert run_step(tree, release_step("image", "name the files after the release"), "PRODUCT" => "skyblip_go", "RELEASE_VERSION" => "0.1.0").last.success?
      assert_equal %w[skyblip-go-0.1.0.full.signed.bin skyblip-go-0.1.0.signed.bin skyblip-go-0.1.0.uf2], Dir.children(File.join(tree, "release")).sort
    end

    Dir.mktmpdir do |tree|
      release_tree(tree, slug: "skyblip-bravo", hub_image: false)
      assert run_step(tree, release_step("image", "name the files after the release"), "PRODUCT" => "skyblip_bravo", "RELEASE_VERSION" => "0.2.0").last.success?
      assert_equal %w[skyblip-bravo-0.2.0.signed.bin skyblip-bravo-0.2.0.uf2], Dir.children(File.join(tree, "release")).sort
    end
  end

  def test_the_release_takes_each_products_images_from_its_own_artifact
    Dir.mktmpdir do |tree|
      image_artifacts(tree)
      output, status = run_step(tree, release_step("files", "every product's images, and nothing else"), RELEASE_ENV)

      assert status.success?, output
      assert_equal %w[skyblip-bravo-0.2.0.signed.bin skyblip-bravo-0.2.0.uf2 skyblip-go-0.2.0.full.signed.bin skyblip-go-0.2.0.signed.bin skyblip-go-0.2.0.uf2],
                   Dir.children(File.join(tree, "release")).sort
      refute workflow("release").dig("jobs", "files", "steps").any? { |step| step.dig("with", "merge-multiple") }
    end
  end

  def test_the_release_refuses_an_artifact_no_product_asks_for
    Dir.mktmpdir do |tree|
      image_artifacts(tree)
      FileUtils.mkdir_p(File.join(tree, "images/image-skyblip_tango"))
      File.write(File.join(tree, "images/image-skyblip_tango/skyblip-tango-0.2.0.signed.bin"), "planted")
      output, status = run_step(tree, release_step("files", "every product's images, and nothing else"), RELEASE_ENV)

      refute status.success?
      assert_includes output, "FAIL: the artifacts are image-skyblip_bravo image-skyblip_go image-skyblip_tango, where the products ask for image-skyblip_bravo image-skyblip_go"
      refute File.exist?(File.join(tree, "release"))
    end
  end

  def test_the_release_refuses_a_file_a_product_does_not_build
    { "embed.js" => "FAIL: image-skyblip_go holds embed.js", "skyblip-go-0.1.0.uf2" => "FAIL: image-skyblip_go holds skyblip-go-0.1.0.uf2" }.each do |planted, failure|
      Dir.mktmpdir do |tree|
        image_artifacts(tree)
        File.write(File.join(tree, "images/image-skyblip_go", planted), "planted")
        output, status = run_step(tree, release_step("files", "every product's images, and nothing else"), RELEASE_ENV)

        refute status.success?, planted
        assert_includes output, failure
      end
    end
  end

  def test_the_release_refuses_a_product_missing_an_image
    Dir.mktmpdir do |tree|
      image_artifacts(tree)
      File.delete(File.join(tree, "images/image-skyblip_bravo/skyblip-bravo-0.2.0.uf2"))
      output, status = run_step(tree, release_step("files", "every product's images, and nothing else"), RELEASE_ENV)

      refute status.success?
      assert_includes output, "FAIL: image-skyblip_bravo has no skyblip-bravo-0.2.0.uf2"
    end
  end

  def test_the_release_files_hold_every_products_images_beside_the_simulator_the_notices_and_the_radio_protocols
    Dir.mktmpdir do |tree|
      release_tree(tree, slug: "skyblip-go", hub_image: true)
      %w[lib/radio_protocols.rb scripts/radio_protocols.rb firmware/products/skyblip_go/PROTOCOLS firmware/products/skyblip_bravo/PROTOCOLS
         firmware/protocols/adsl/version.h firmware/protocols/adsb/version.h firmware/protocols/alptas/version.h].each do |file|
        FileUtils.mkdir_p(File.join(tree, File.dirname(file)))
        FileUtils.cp(File.join(ROOT, file), File.join(tree, file))
      end
      FileUtils.mkdir_p(File.join(tree, "release"))
      %w[skyblip-go-0.2.0.signed.bin skyblip-bravo-0.2.0.signed.bin].each { |file| File.write(File.join(tree, "release", file), file) }
      output, status = run_step(tree, release_step("files", "add the simulator, the notices, the radio protocols and the checksums"), RELEASE_ENV)
      assert status.success?, output

      listed = File.readlines(File.join(tree, "release/SHA256SUMS"), chomp: true).map { |line| line.split("  ", 2).last }
      assert_equal Dir.children(File.join(tree, "release")).sort - %w[SHA256SUMS], listed.sort
      assert_includes listed, "skyblip-bravo-0.2.0.signed.bin"
      assert_includes listed, "scenarios.json"
      versions = %w[adsl adsb alptas].to_h { |stem| [ stem, File.read(File.join(ROOT, "firmware/protocols/#{stem}/version.h"))[RadioProtocols::DECLARATION, 1] ] }
      assert_includes File.read(File.join(tree, "release/PROTOCOLS.md")), "- `skyblip-bravo`: ADS-B #{versions["adsb"]}\n- `skyblip-go`: ADS-L #{versions["adsl"]}, ALP-TAS #{versions["alptas"]}\n"
      assert_includes workflow("release").dig("jobs", "files", "needs"), "image"
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

  def test_the_jobs_holding_the_production_key_run_no_emscripten_and_write_nothing
    jobs = workflow("release").fetch("jobs")
    image = jobs.fetch("image")

    assert_includes image.fetch("needs"), "simulator"
    refute image.fetch("steps").any? { |step| step["uses"].to_s.include?("setup-emsdk") || step["run"].to_s.include?("make -C firmware browser") }
    refute image.key?("permissions")
    assert_equal({ "contents" => "read" }, workflow("release").fetch("permissions"))
    assert_equal [ "draft" ], jobs.select { |_name, job| job.dig("permissions", "contents") == "write" }.keys
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

    def step_outputs(tree, step, env = {})
      outputs = File.join(tree, "outputs")
      File.write(outputs, "")
      output, status = run_step(tree, step, env.merge("GITHUB_OUTPUT" => outputs))
      assert status.success?, output
      File.readlines(outputs, chomp: true).to_h { |line| line.split("=", 2) }
    end

    def declared_products
      Dir.glob(File.join(ROOT, "firmware/products/*/CMakeLists.txt")).select { |cmake| File.read(cmake).match?(/^skyblip_product_board\(/) }
        .map { |cmake| File.basename(File.dirname(cmake)) }.sort
    end

    def image_artifacts(tree)
      { "skyblip_go" => %w[uf2 signed.bin full.signed.bin], "skyblip_bravo" => %w[uf2 signed.bin] }.each do |product, endings|
        FileUtils.mkdir_p(File.join(tree, "images/image-#{product}"))
        endings.each { |ending| File.write(File.join(tree, "images/image-#{product}/#{product.tr("_", "-")}-0.2.0.#{ending}"), ending) }
      end
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
