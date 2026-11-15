class ResolveErrorTool < ApplicationTool
  tool_name "resolve_error"
  title "Resolve an error"
  description "Mark an error from list_errors resolved, once the fix for it is deployed. list_errors leaves it out until " \
              "it happens again, when it comes back with reopened. Resolving an error already resolved moves its " \
              "resolution to now. For admins only, and each resolution is logged."
  input_schema properties: { fingerprint: { type: "string", description: "The error's fingerprint, as list_errors gives it" } },
    required: [ "fingerprint" ]
  annotations(read_only_hint: false, destructive_hint: false, idempotent_hint: true, open_world_hint: false)

  def self.call(server_context:, fingerprint:)
    if (resolver = admin(server_context))
      if (error = Event::Error.find_by(fingerprint:))
        error.resolve(by: resolver)
        json error.to_h
      else
        refused "No error has the fingerprint #{fingerprint.to_s.truncate(40)}"
      end
    else
      admins_only
    end
  end
end
