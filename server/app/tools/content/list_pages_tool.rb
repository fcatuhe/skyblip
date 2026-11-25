class Content::ListPagesTool < ApplicationTool
  tool_name "list_pages"
  title "List the pages of skyblip.eu"
  description "Every page of skyblip.eu in one language: its title, its address, the address of its Markdown, its description, " \
              "the language it is written in and the day it last changed. A page with no translation is listed in English. " \
              "Read one with get_page."
  input_schema properties: { language: LANGUAGE }
  annotations(**READING)

  def self.call(server_context:, language: I18n.default_locale.to_s)
    json Content::Page.read_in(language).map { facts_of(it) }
  end

  def self.facts_of(page)
    { title: page.title, url: page.canonical_url, markdown_url: page.markdown_url, description: page.description,
      language: page.locale, updated_on: page.updated_on.iso8601 }
  end
end
