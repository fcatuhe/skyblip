CI.run do
  step "Setup", "bin/setup --skip-server"

  step "Style: Ruby", "bin/rubocop"
  step "Style: No smart quotes", "bin/smart_quotes find"

  step "Boot: Eager loads without the development bundle", "bin/eager_load_check"

  step "Security: Gem audit", "bin/bundler-audit"
  step "Security: Importmap vulnerability audit", "bin/importmap audit"
  step "Security: Brakeman code analysis", "bin/brakeman --quiet --no-pager --exit-on-warn --exit-on-error"

  step "Content: sitemap", "bin/rails content:sitemap:check"

  step "Tests: Rails", "bin/rails test"
  step "Tests: System", "bin/rails test:system"
  step "Tests: browser protocols and firmware words", "node --test test/javascript/test_*.mjs"
end
