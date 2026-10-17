# Deployment

Pushing to `main` deploys: the `deploy` job in `.github/workflows/server.yml` runs `kamal deploy` from an arm64 runner once `bin/ci` is green, with the secrets of the GitHub `server_production` environment. A push that touches nothing under `server/` does not deploy, and no firmware release does either.

By hand, from `server/`, with Bitwarden unlocked:

```sh
mise run kamal deploy
```

## Where it runs

On the crooz server, beside crooz itself: the same Debian host, user `debian`, and the same kamal-proxy, which routes `skyblip.eu` to this app. `config/deploy.yml` names the host `SKYBLIP_SERVER_IP` when it is set, as in the deploy job, and the `crooz` SSH alias otherwise, which the dotfiles render into `~/.ssh/config` from Bitwarden. Kamal reads that file, so a deploy by hand needs no variable.

The image is arm64, because crooz is. The registry is a container Kamal runs on the machine deploying and tunnels to the server over SSH, so there is no hub and no registry password, but Docker has to be running locally. An arm64 machine builds locally, and any other builds over SSH on crooz itself, which `config/deploy.yml` switches on by reading `RUBY_PLATFORM`. Kamal builds `server/` from a clean clone of the commit, so commit before deploying by hand.

Thruster serves on port 80 inside the container and Puma runs Solid Queue in its own process (`SOLID_QUEUE_IN_PUMA`). The four SQLite databases, primary, cache, queue and cable, live in the `skyblip_storage` volume, and `bin/docker-entrypoint` runs `db:prepare` before the server starts. There is no backup yet: #131 adds Litestream and kamal-backup before the first registration that matters.

## Secrets

`.kamal/secrets` reads `RAILS_MASTER_KEY` from the environment only, and the `pre-build` hook stops a deploy that has none, so a plain `bin/kamal deploy` fails before it builds. Commands that only read from the server, such as `bin/kamal app details`, run as they are. `mise run kamal` hands it the production key, see [Credentials](README.md#credentials).

The GitHub `server_production` environment holds `SSH_PRIVATE_KEY`, the private half of the Bitwarden SSH key `skyblip-github-actions`, authorized for `debian` on crooz through the vps-ansible inventory, `SKYBLIP_SERVER_IP`, the crooz server's address, and `RAILS_MASTER_KEY`, the production key. Its deployment branch policy allows `main` only, so no other branch's workflow can read them. The firmware signing keys live apart, in `firmware_development` (`MCUBOOT_DEVELOPMENT_SIGNING_KEY`) and `firmware_production` (`MCUBOOT_PRODUCTION_SIGNING_KEY`, a required reviewer, `v*` tags only), so no server job runs where a signing key is.

## First deployment

1. Create the production credentials, see [Credentials](README.md#credentials).
2. Fill the GitHub `server_production` environment as above.
3. Point `skyblip.eu` at crooz, so kamal-proxy can get its certificate.
4. `mise run kamal setup` once, then pushes to `main` take over.
5. Retire the Pages deployment and its settings.

## Releases

Both development pre-releases and production releases attach `skyblip_simulator.js`, `skyblip_simulator.wasm`, `embed.js` and `scenarios.json` beside the firmware images, before `SHA256SUMS`, so production's attestation covers them. The production WASM is built in its own job, away from the runner holding the signing key. A release without these files cannot run in the simulator, and publishing one never redeploys the server.
