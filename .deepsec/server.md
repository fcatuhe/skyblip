## What this codebase does

skyblip.eu, one Rails 8.1 app (Ruby 4, SQLite, Hotwire, Importmap, Kamal) for skyBlip, open-source electronic conspicuity trackers for light aircraft. It serves public content, `/briefing` (a browser app pilots use over Web Bluetooth and Web Serial to activate, set up, update and inspect their devices), an admin area, and an OAuth-protected MCP endpoint at `/mcp` for admins' coding agents. There are no pilot accounts: only admins sign in.

## Auth shape

- `Authentication` concern (Rails 8 generator): `require_authentication` by default, `allow_unauthenticated_access` opts out. `Briefing::`, `Activation::`, `Attestation::` base controllers, `EventsController`, `Content::PagesController` and `Releases::AssetsController` are public on purpose.
- `Admin::ApplicationController#ensure_admin` gates every admin controller, `OAuth::AuthorizationsController` (the consent page) included. `Current.user.admin?` is the only role check.
- `/mcp` (`McpsController < ActionController::API`) is guarded by the `mcp` gem with `OAuth::Token` as token verifier. Each tool in `app/tools/` is expected to re-check `ApplicationTool.admin(server_context)` so a demoted admin reads nothing.
- Device trust: units hold a 32-byte public key (`Unit::Key`, `Identity::PublicKey`), the server signs with `Identity::ServerKey`. Activation and attestation are challenge/response flows: a server nonce from `SingleUse` lives 5 minutes and is spent once by a conditional delete, and the device signs over it.
- OAuth 2.1: authorization code + PKCE S256, public clients only, `client_id` must be one of `config.x.oauth_client_ids` (CIMD), codes single-use, refresh tokens rotate and reuse revokes the grant, only digests stored. `app/models/oauth/README.md` is the design.

## Threat model

Highest impact: minting or using an OAuth token for `/mcp` without being an admin, or keeping access after demotion, a password change or Disconnect. Next: registering or activating a unit, or getting a server-signed serial or attestation, without holding the device's private key, or replaying a nonce. Next: writing forged events or captures that poison admin views, or reach an admin's agent as instructions through MCP tool output. Next: exhausting the single SQLite-backed server (3 Puma threads) from a public endpoint, by request count or by bytes stored. Also: SSRF through OAuth client metadata or release downloads, and open redirects in routes, OAuth or session return paths.

## Project-specific patterns to flag

- A controller under `Briefing::`, `Activation::`, `Attestation::` or `EventsController` that changes data tied to a unit without a device signature or a spent nonce.
- A public controller that does work (an outbound fetch, a write, a job) without `rate_limit`, or that stores what a client sends without a bound on its size or type.
- A nonce, code or token check done with `find_by` + later `update`/`destroy` instead of the single conditional update or `delete_all == 1` used by `SingleUse#spend`.
- An MCP tool in `app/tools/` that skips `admin(server_context)` or returns data before the check, or text a client wrote that reaches a tool's output without the length limits `Event::Journal` applies.
- A redirect, in `config/routes.rb` or a controller, built from the request path or a param without pinning the host.

## Known false-positives

- Public `Briefing::` pages and `Releases::AssetsController` (`skip_forgery_protection`, unauthenticated): they serve the briefing and release assets on purpose.
- `EventsController` is unauthenticated by design: browsers post their journal without an account. Its volume, not its openness, is the risk.
- `app/javascript/**`: browser code talking to the device over Bluetooth. `Math.random` in the simulators and `fetch` to same-origin paths are not security-relevant.
