require "minitest/autorun"
require "fileutils"
require "tmpdir"
require_relative "../../lib/firmware_tree"

class FirmwareTreeTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_the_version_is_the_triple_firmware_version_assigns
    Dir.mktmpdir do |firmware|
      File.write(File.join(firmware, "VERSION"), "# VERSION_MAJOR = 9\nVERSION_MAJOR = 0\nVERSION_MINOR = 2\nPATCHLEVEL = 10\nVERSION_TWEAK = 0\n")

      assert_equal "0.2.10", FirmwareTree.new(firmware).version
    end
  end

  def test_a_version_file_without_a_patch_level_is_refused
    Dir.mktmpdir do |firmware|
      File.write(File.join(firmware, "VERSION"), "VERSION_MAJOR = 0\nVERSION_MINOR = 2\n")
      error = assert_raises(ArgumentError) { FirmwareTree.new(firmware).version }

      assert_equal "No version triple in #{firmware}/VERSION", error.message
    end
  end

  def test_a_product_is_a_directory_whose_cmakelists_declares_its_board
    Dir.mktmpdir do |firmware|
      { "skyblip_go" => "skyblip_product_board(t_echo_plus)\n", "skyblip_bravo" => "skyblip_product_board(meshtracker_x1)\n", "common" => "" }.each do |product, board|
        FileUtils.mkdir_p(File.join(firmware, "products", product))
        File.write(File.join(firmware, "products", product, "CMakeLists.txt"), "project(#{product})\n#{board}")
      end

      assert_equal %w[skyblip_bravo skyblip_go], FirmwareTree.new(firmware).products
    end
  end

  def test_a_tree_with_no_product_is_refused
    Dir.mktmpdir do |firmware|
      FileUtils.mkdir_p(File.join(firmware, "products/common"))
      File.write(File.join(firmware, "products/common/CMakeLists.txt"), "project(common)\n")
      error = assert_raises(ArgumentError) { FirmwareTree.new(firmware).products }

      assert_equal "No directory in #{firmware}/products declares a board", error.message
    end
  end

  # Zephyr reads products/<product>/VERSION, and a copy there would sign one version and report another.
  def test_every_product_carries_the_firmware_version_and_lists_its_radio_protocols
    FirmwareTree.new(File.join(ROOT, "firmware")).products.each do |product|
      assert_equal "../../VERSION", File.readlink(File.join(ROOT, "firmware/products", product, "VERSION")), product
      assert File.file?(File.join(ROOT, "firmware/products", product, "PROTOCOLS")), "#{product} has no PROTOCOLS"
    end
  end
end
