require "open3"

# INFO: fc 19nov26 deploy jobs queue in the order their runs reach them, so an older commit could replace a newer one in production
class DeployOrder
  Unknown = Class.new(StandardError)

  COMMIT = /\A\h{40}\z/

  def self.running_version(server_dir)
    output, status = Open3.capture2("bundle", "exec", "kamal", "app", "version", "--quiet", chdir: server_dir)
    raise Unknown, "kamal app version failed" unless status.success?

    output.lines.map(&:strip).find { !it.empty? } || ""
  end

  attr_reader :commit, :running

  def initialize(commit:, running:, repository:)
    @commit, @running, @repository = commit, running, repository
  end

  def deploy? = running.empty? || running == commit || !ancestor?(commit, running)

  private
    def ancestor?(older, newer)
      raise Unknown, "production runs #{newer.inspect}, which names no commit" unless COMMIT.match?(newer)

      _, status = Open3.capture2e("git", "-C", @repository, "merge-base", "--is-ancestor", older, newer)
      case status.exitstatus
      when 0 then true
      when 1 then false
      else raise Unknown, "git does not know #{newer}, the version production runs"
      end
    end
end
