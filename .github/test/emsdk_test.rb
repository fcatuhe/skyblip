require "minitest/autorun"
require "fileutils"
require "tmpdir"
require_relative "../../lib/emsdk"

class EmsdkTest < Minitest::Test
  def test_an_emcc_at_the_pin_is_used_as_it_is
    in_sandbox do |sandbox|
      emcc = fake_emcc(sandbox, "6.0.9")

      assert_equal emcc, emcc_for(sandbox, emcc, "6.0.9")
      refute File.exist?(File.join(sandbox, "emsdk")), "nothing was installed"
    end
  end

  def test_a_missing_emsdk_is_cloned_and_the_pin_installed
    in_sandbox do |sandbox|
      emcc = emcc_for(sandbox, "no-such-emcc", "6.0.9")

      assert_equal File.join(sandbox, "emsdk/upstream/emscripten/emcc"), emcc
      assert_includes log(sandbox), "git clone"
      assert_includes log(sandbox), "./emsdk activate 6.0.9"
    end
  end

  def test_an_emsdk_at_the_pin_builds_without_touching_the_network
    in_sandbox do |sandbox|
      emcc_for(sandbox, "no-such-emcc", "6.0.9")
      File.write(File.join(sandbox, "log"), "")

      emcc_for(sandbox, "no-such-emcc", "6.0.9")
      assert_empty log(sandbox)
    end
  end

  # A bump on main reaches the emsdk installed for the version before it.
  def test_a_bump_pulls_emsdk_and_installs_the_new_pin
    in_sandbox do |sandbox|
      emcc = emcc_for(sandbox, "no-such-emcc", "6.0.9")
      File.write(File.join(sandbox, "log"), "")

      assert_equal emcc, emcc_for(sandbox, fake_emcc(sandbox, "6.0.9"), "7.0.0")
      assert_includes log(sandbox), "git -C"
      assert_includes log(sandbox), "./emsdk install 7.0.0"
    end
  end

  def test_an_install_that_leaves_another_version_fails
    in_sandbox(installs: "1.0.0") do |sandbox|
      error = assert_raises(RuntimeError) { emcc_for(sandbox, "no-such-emcc", "6.0.9") }
      assert_includes error.message, "runs Emscripten \"1.0.0\" after installing 6.0.9"
    end
  end

  private
    def emcc_for(sandbox, candidate, version)
      File.open(File.join(sandbox, "log"), "a") do |log|
        Emsdk.emcc(candidate:, version:, directory: File.join(sandbox, "emsdk"), repository: File.join(sandbox, "upstream"), log:)
      end
    end

    def log(sandbox) = File.read(File.join(sandbox, "log"))

    def in_sandbox(installs: nil)
      Dir.mktmpdir do |sandbox|
        upstream = File.join(sandbox, "upstream")
        FileUtils.mkdir_p(upstream)
        File.write(File.join(upstream, "emsdk"), <<~SCRIPT)
          #!/usr/bin/env ruby
          require "fileutils"
          command, version = ARGV
          if command == "install"
            FileUtils.mkdir_p(File.join(__dir__, "upstream/emscripten"))
            emcc = File.join(__dir__, "upstream/emscripten/emcc")
            File.write(emcc, "#!/bin/sh\\necho 'emcc (Emscripten gcc/clang-like replacement) #{installs || '#{version}'} (0abc)'\\n")
            File.chmod(0o755, emcc)
          end
        SCRIPT
        File.chmod(0o755, File.join(upstream, "emsdk"))
        system("git", "init", "-q", upstream, exception: true)
        system("git", "-C", upstream, "add", "emsdk", exception: true)
        system("git", "-C", upstream, "-c", "user.name=test", "-c", "user.email=test@example.com", "commit", "-qm", "emsdk", exception: true)
        File.write(File.join(sandbox, "log"), "")
        yield sandbox
      end
    end

    def fake_emcc(sandbox, version)
      emcc = File.join(sandbox, "emcc-#{version}")
      File.write(emcc, "#!/bin/sh\necho 'emcc (Emscripten gcc/clang-like replacement) #{version} (0abc)'\n")
      File.chmod(0o755, emcc)
      emcc
    end
end
