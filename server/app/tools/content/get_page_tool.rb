class Content::GetPageTool < ApplicationTool
  tool_name "get_page"
  title "Read a page of skyblip.eu"
  description "A page of skyblip.eu in Markdown, whole, under the address it lives at. Its links lead to the Markdown of the " \
              "pages they name, which get_page reads too."
  input_schema properties: {
    url: { type: "string", maxLength: 300, description: "The page's address, such as https://skyblip.eu/fr/go, /fr/go or /fr/go.md" }
  }, required: [ "url" ]
  annotations(**READING)

  def self.call(server_context:, url:)
    if (path = URI(url).path) && (page = Content::Page.at(path))
      text "Source: #{page.canonical_url}\n\n#{page.markdown}"
    else
      refused "skyblip.eu has no page at #{url.truncate(80)}. list_pages names every page."
    end
  rescue URI::Error
    refused "#{url.truncate(80)} is not an address. list_pages names every page."
  end
end
