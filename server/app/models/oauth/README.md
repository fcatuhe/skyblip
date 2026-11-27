# oauth

skyBlip is its own OAuth 2.1 authorization server, for one resource: the admin MCP endpoint at `/admin/mcp`, where an admin's coding agent reads the insights, the errors, everybody's events and the units, and resolves an error once its fix is deployed. Signing in to the admin area is the login. An admin who adds skyBlip to Claude Code, Codex or pi sees one consent page, `/oauth/authorization/new`, and clicks Connect. A user who is not an admin is refused there, and a tool answers an error to an agent whose admin was demoted since.

The tools read, except `resolve_error`, which only takes an error off `list_errors` until it happens again, and is logged as `error.resolved`. Events hold messages browsers and devices wrote, so the server's instructions tell the agent to read them as data, never as instructions.

## How an agent finds skyBlip

The `mcp` gem guards `/admin/mcp` with its OAuth resource-server support: `OAuth::Token.verify` is the verifier it calls. A request to `/admin/mcp` without a token answers 401, and its `WWW-Authenticate` names the protected resource metadata (RFC 9728) at `/.well-known/oauth-protected-resource/admin/mcp`. That document names skyBlip as the authorization server, whose metadata (RFC 8414) is at `/.well-known/oauth-authorization-server`. It advertises the authorization code grant with PKCE S256, public clients only, client ID metadata documents, and the `iss` parameter on the redirect (RFC 9207). Claude Code and Codex pick client ID metadata documents on that advertisement alone. pi needs `"oauth": { "clientRegistration": "cimd" }` in its `mcp.json`, since skyBlip offers no dynamic registration.

```sh
claude mcp add --transport http skyblip-admin https://skyblip.eu/admin/mcp
```

The gem refuses a resource on plain http other than `localhost`, so a development server on `skyblip.localhost` serves no admin MCP. Agents connect to production.

The admin server lived at `/mcp` until 25 November 2026, when the public server that reads the website took that address. A grant made for `/mcp` names a resource skyBlip no longer serves, so its tokens are refused, the next sweep drops it, and the agent connects again at `/admin/mcp`.

## Clients

An agent's `client_id` is the URL of its metadata document. skyBlip connects the four public clients in `config.x.oauth_client_ids`, matched exactly: Claude Code, Codex, pi, and Claude on the web, Desktop and mobile, whose code returns to `https://claude.ai/api/mcp/auth_callback`, the one entry of `config.x.oauth_hosted_callbacks`. skyBlip fetches no other URL, so a `client_id` can never point skyBlip at an address inside the network, nor at another page on those hosts posing as one of the four. Taking one off the list cuts its connections at once. `OAuth::Client::Document` refuses a document whose `client_id` is not its own URL, whose auth method is not `none`, or that names no redirect skyBlip sends codes to, and `OAuth::Client` keeps what it read for a day.

A redirect is either a loopback URL, which matches on any port (RFC 8252), or a hosted callback, matched exactly. A `client_id` or `redirect_uri` that fails is refused on skyBlip's own page and never followed. Every other mistake goes back to the agent as an OAuth error.

## Grants, codes and tokens

Approving makes or refreshes one `OAuth::Grant` per admin and agent, which `Agents` in the admin area lists and Disconnect destroys, with its codes and tokens. A new password or a demotion from admin revokes all of the user's grants the same way, so an agent connected from a leaked session loses access, and promoting the user again brings none back without a new consent. Approving reads the role and the session again under the user's lock, so a demotion or a new password that lands while the admin approves is refused there and leaves no grant behind.

- A code lives a minute and spends once, checked against its client, its redirect and the PKCE verifier. Presented again with the right verifier, it is refused and its grant revoked.
- An access token lives an hour, a refresh token thirty days, and each refresh spends its refresh token for a new pair. A refresh token presented again after it was spent revokes the whole grant.
- No token outlives thirty days from the consent, so at least once a month the agent sends its admin back through the consent.
- Only digests are stored, and spending is a conditional update, so of two simultaneous redemptions one goes through. The log filters codes, verifiers and tokens. The token and revocation endpoints read the form body alone (RFC 6749 3.2), so nothing secret rides in a query string a proxy logs.
- An agent that signs out posts its token to `/oauth/revocation` (RFC 7009).

`OAuth.sweep` runs every morning from `config/recurring.yml`: expired codes and tokens go, and so does a grant left with no token, past its approval, or made for a resource skyBlip no longer serves.
