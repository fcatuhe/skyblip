# oauth

skyBlip is its own OAuth 2.1 authorization server, for one resource: the MCP endpoint at `/mcp`, where an admin's coding agent reads the insights, the errors, everybody's events and the units. Signing in to the admin area is the login. An admin who adds skyBlip to Claude Code, Codex or pi sees one consent page, `/oauth/authorization/new`, and clicks Connect. A user who is not an admin is refused there, and a tool answers an error to an agent whose admin was demoted since.

The tools are read only. Events hold messages browsers and devices wrote, so the server's instructions tell the agent to read them as data, never as instructions.

## How an agent finds skyBlip

The `mcp` gem guards `/mcp` with its OAuth resource-server support: `OAuth::Token.verify` is the verifier it calls. A request to `/mcp` without a token answers 401, and its `WWW-Authenticate` names the protected resource metadata (RFC 9728) at `/.well-known/oauth-protected-resource/mcp`. That document names skyBlip as the authorization server, whose metadata (RFC 8414) is at `/.well-known/oauth-authorization-server`. It advertises the authorization code grant with PKCE S256, public clients only, client ID metadata documents, and the `iss` parameter on the redirect (RFC 9207). Claude Code and Codex pick client ID metadata documents on that advertisement alone. pi needs `"oauth": { "clientRegistration": "cimd" }` in its `mcp.json`, since skyBlip offers no dynamic registration.

```sh
claude mcp add --transport http skyblip https://skyblip.eu/mcp
```

The gem refuses a resource on plain http other than `localhost`, so a development server on `skyblip.localhost` serves no MCP. Agents connect to production.

## Clients

An agent's `client_id` is the URL of its metadata document. skyBlip connects the four public clients in `config.x.oauth_client_ids`, matched exactly: Claude Code, Codex, pi, and Claude on the web, Desktop and mobile, whose code returns to `https://claude.ai/api/mcp/auth_callback`, the one entry of `config.x.oauth_hosted_callbacks`. skyBlip fetches no other URL, so a `client_id` can never point skyBlip at an address inside the network, nor at another page on those hosts posing as one of the four. Taking one off the list cuts its connections at once. `OAuth::Client::Document` refuses a document whose `client_id` is not its own URL, whose auth method is not `none`, or that names no redirect skyBlip sends codes to, and `OAuth::Client` keeps what it read for a day.

A redirect is either a loopback URL, which matches on any port (RFC 8252), or a hosted callback, matched exactly. A `client_id` or `redirect_uri` that fails is refused on skyBlip's own page and never followed. Every other mistake goes back to the agent as an OAuth error.

## Grants, codes and tokens

Approving makes or refreshes one `OAuth::Grant` per admin and agent, which `Agents` in the admin area lists and Disconnect destroys, with its codes and tokens.

- A code lives a minute and spends once, checked against its client, its redirect and the PKCE verifier. Presented again with the right verifier, it is refused and its grant revoked.
- An access token lives an hour, a refresh token thirty days, and each refresh spends its refresh token for a new pair. A refresh token presented again after it was spent revokes the whole grant.
- No token outlives thirty days from the consent, so at least once a month the agent sends its admin back through the consent.
- Only digests are stored, and spending is a conditional update, so of two simultaneous redemptions one goes through. The log filters codes, verifiers and tokens. The token and revocation endpoints read the form body alone (RFC 6749 3.2), so nothing secret rides in a query string a proxy logs.
- An agent that signs out posts its token to `/oauth/revocation` (RFC 7009).

`OAuth.sweep` runs every morning from `config/recurring.yml`: expired codes and tokens go, and so does a grant left with no token or past its approval.
