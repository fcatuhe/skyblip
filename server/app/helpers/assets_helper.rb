# INFO: fc 25nov26 Turbo reloads a page whose tracked assets changed, which ends its Bluetooth links, so freshness.js decides when instead
module AssetsHelper
  def assets_digest
    Digest::SHA256.hexdigest([ importmap_json, site_stylesheet_link_tags ].join("\n")).first(16)
  end

  def importmap_tags
    safe_join [
      tag.script(importmap_json.html_safe, type: "importmap"),
      javascript_importmap_module_preload_tags,
      javascript_import_module_tag("application")
    ], "\n"
  end

  private
    def importmap_json = Rails.application.importmap.to_json(resolver: self)
end
