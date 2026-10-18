# server

[skyblip.eu](https://skyblip.eu) runs as one Rails app on the crooz server. Public content lives under `Content`, and `/briefing` links the device update page, Bluetooth console and editable simulator. Users sign in to an admin area, and `/activate` gives each unit its serial (#126).

Every command here runs from this directory, not from the repo root.

## Running it

```sh
mise trust && mise install   # Ruby, and fnox
bin/setup                    # gems, the databases, then bin/dev
bin/dev                      # http://skyblip.localhost:8119
bin/ci                       # the gate before handoff, the checks CI runs as separate jobs
bin/rails test:system        # Cuprite, on the Chrome or Chromium it finds, BROWSER_PATH otherwise
bin/screenshots              # retake the device screens off the firmware next door
```

Rails 8.1 on Ruby 4, SQLite with Solid Cache, Queue and Cable, Hotwire over Propshaft and Importmap, Minitest with Cuprite for system tests and VCR for GitHub, Kamal to deploy ([DEPLOYMENT.md](DEPLOYMENT.md)).

Tests that reach GitHub replay a cassette from `test/vcr_cassettes/github/`, system tests included: the app server runs in the test process, so the cassette a test inserts answers the request a Turbo Frame makes. `CASSETTES_RECORD=1` records them again against the real API.

## Credentials

The credentials keys live in Bitwarden, in the secure note `skyblip-repository`, which [fnox](https://fnox.jdx.dev) reads through the `bw` CLI as `fnox.toml` maps it. It is named after the repository rather than `skyblip` alone because `bw` finds an item by searching names, usernames and URLs, and a login for the site would match too.

- `config/master.key` decrypts the development credentials, `config/credentials.yml.enc`, and stays on disk. The dotfiles bootstrap writes it from the note's `RAILS_DEVELOPMENT_KEY`, mode 600. A worktree has none, so `bin/setup --skip-server`, run once inside one made by `git wt`, links it from the main checkout.
- The production key never touches the disk. It is the note's `RAILS_PRODUCTION_KEY`, which `mise.toml` grants to two tasks only, as `RAILS_MASTER_KEY`, the name Rails and Kamal read: `mise run kamal ...` and `mise run credentials:production`. Rails reads `RAILS_MASTER_KEY` before any key file, whatever the environment, so it must never reach a development command, and nothing else sees it: not the shell, `mise env`, the shims, or an agent. The tasks' output shows it as `[redacted]`, except the credentials editor, which needs the terminal.

The production credentials do not exist yet. To create them, add `RAILS_PRODUCTION_KEY` to the note, 32 hexadecimal characters as `ruby -rsecurerandom -e 'puts SecureRandom.hex(16)'` prints them, then run `mise run credentials:production`: Rails finds the key in the environment, writes no key file, and creates `config/credentials/production.yml.enc` with a `secret_key_base`. Commit that file, and give the same key to the GitHub `server_production` environment as `RAILS_MASTER_KEY`.

## Admin and analytics

A user has a first name, a last name, an email and a password, and a role: `user` or `admin`. Nothing links to the sign-in page, `/session/new`, which robots are asked not to index. Once signed in, an admin finds `Admin` in the site nav, which opens the admin area: analytics, the users, and Mission Control's jobs at `/admin/jobs`. Every admin controller inherits `Admin::ApplicationController`, which answers `403` to anyone but an admin, the way Fizzy guards its own, and Mission Control is mounted on it too. There is no sign-up and no password reset: an admin creates the accounts and sets their passwords. The first admin is created in the production console, `mise run kamal console`:

```ruby
User.create!(first_name: "[first name]", last_name: "[last name]", email: "[email]", password: "[password]", role: :admin)
```

Ahoy records visits and events in their own database, `analytics`, from the server (every controller action) and from the browser (`app/javascript/ahoy-module.js`, posting to `/hoy`). Each visit is then computed into an `Analytics::Visit`, which knows its pages, duration, channel, section (`public`, `briefing` or `admin`) and segment. The segment is how your own browsing stays out of the numbers: a visit is `internal` when it belongs to an admin, or comes from a browser an admin once signed in from, `user` for any other account, `non_user` otherwise. The `Admin` link opens the overview on `non_user`. Development and test track nothing, `Ahoy.api_only` is on there. Firmware downloads are not tracked, so their year-long public cache never carries a cookie.

Visits are placed by IP with the MaxMind GeoLite City database, `storage/GeoLite2-City.mmdb`, which `Maxmind::UpdateGeoliteCityDatabaseJob` downloads and keeps current. The job is scheduled in `config/recurring.yml` only once the production credentials hold the MaxMind account, so until then visits keep no place and nothing fails:

```yaml
maxmind:
  account_id: [account id]
  license_key: [license key]
```

`mise run credentials:production` adds them. The first nightly run after the next deploy downloads the database.

## Activation

A unit transmits nothing until it holds a serial that this server signed for it, and `/activate` (`content/pages/activate.*`) is where it gets one. The wire protocol, both halves of it, is [`schemas/activation.v1.schema.json`](../schemas/activation.v1.schema.json): the page asks the unit for a nonce, `POST /activation/challenges` signs it, the unit asks on its glass and signs its registration with its own key, `POST /activation/registrations` checks that signature and answers the serial with an unlock code, and the unit stores the code. [`activation.v1.vectors.json`](../schemas/activation.v1.vectors.json) is one exchange signed with the public test key, which `test/models/activation/message_test.rb` holds OpenSSL to and the firmware's host suite holds Monocypher to: Ed25519 is deterministic, so two libraries that sign the vectors alike sign everything alike.

`Unit` is the DEVICEID and its serial, dense from 1 to 65535 (0 would put 0x000000 on the air, which decoders read as no address), and `Unit::Key` the public keys it registered with, `active`, `pending` or `revoked`. A new DEVICEID gets the next serial and its key is bound. The same key gets the same serial in a fresh unlock code. Another key is refused as `other_key` and kept as `pending`, with the date, the firmware and the IP it came from. In the admin area, `Units` lists them: promote makes a pending key the active one and revokes the old, revoke leaves the unit with no key, so the next registration binds its key as a first one does. A revoked key stays revoked: it is refused as `revoked_key`, and the page shows the DEVICEID and the start of the key, which is what an owner writes in with and what the `Units` page lists. The unlock code is signed inside the transaction that bound the key, so a revocation lands before it or after it. `Activation::Challenge` is the server nonce, kept five minutes, used once, and forgotten hourly by `config/recurring.yml`.

Both endpoints take ten requests a minute per IP (`rate_limit`, on Solid Cache). Past 50 new units in a day, a new DEVICEID is refused as `daily_cap`. The enrollment that reaches the cap logs an error, and the admin `Units` page carries a banner until midnight UTC: the app has no mailer or error tracker to alert by, so those two are the alert. A server nonce is spent only by a registration its unit signed, since it travels in clear over Bluetooth.

Only skyblip.eu signs for a unit. It holds one Ed25519 key, the server key, which signs every message a unit must trust from it about its identity: the challenge and the unlock code today, any later command that touches identity tomorrow, each under its own `skyblip:<purpose>:v1` label (`Identity::Message`). Every device build, a pull request, `main`, a release or a local build, carries only its public half, `firmware/keys/server.pub`, so a build needs no secret to be activated. The seed, 64 hexadecimal characters, lives in the production credentials alone:

```yaml
identity:
  server_key: [seed]
```

`openssl rand -hex 32` makes one: any 32 random bytes are a seed. Add it with `mise run credentials:production`. `Identity::ServerKey` derives the public key from it, and names it by the first four bytes of the SHA-256 of that public key, which is how the unit names the key it trusts in its `challenge` reply. The admin `Units` page shows both, to check against `firmware/keys/server.pub`. To get the public half before a deploy, in your own terminal:

```sh
read -rs seed && ruby -ropenssl -e 'k = OpenSSL::PKey.new_raw_private_key("ED25519", [ARGV[0]].pack("H*")).raw_public_key; puts k.unpack1("H*"), OpenSSL::Digest::SHA256.digest(k)[0, 4].unpack1("H*")' "$seed"; unset seed
```

Development and test sign with the public test key of the vectors (`config/initializers/identity.rb`), which only the firmware's host suite and simulator trust, so a local server can never activate a real unit, nor hand one a serial the registry gave another. With no key at all, the activation endpoints fail with an error and the `Units` page says so.

The registration also says whether the unit runs a release build, signed with the rest, and the `Units` page shows it as the build, release or development. Nothing on the air tells them apart: a development unit is a real aircraft with a real serial.

## Content

Pages are files, not database rows: `content/pages/<slug>.html.erb`, with YAML frontmatter for `title`, `nav_title`, `description` and an optional localized `slug`. A page is bilingual by convention, `<slug>.html.erb` and `<slug>.fr.html.erb`, and the locale switcher offers a language only where the twin exists.

`Content::Page` reads the collection, and the content route accepts only existing localized slugs, leaving application routes separate. Every published page is listed in the sitemap, whether another page links to it or not. Run `bin/rails content:sitemap:generate` after changing the collection. `bin/rails content:sitemap:check` and the test suite catch drift. Canonical URLs always use `config.x.public_url`, not the incoming request host.

The site sells one product, the skyBlip Go, and every page is built from the same sections: a centred `page-hero`, then `band`s that alternate with `band--tint`, each opened by a `section-title` (`sections.css`). The nav carries the four pages a buyer reads, `features`, `buy`, `guides` and `about`, then `manage` and the simulator (`skyblip-go`, served as `/try`) on the right. The footer carries everything, the guides included. `index` is the product page, `features` walks through what the device does with a link per feature to the guide paragraph that explains it, `buy` says where to get one and how to install it, `guides` indexes the subject pages (`radar`, `instruments`, `controls`, `power`, `phone`, `radio` and `displays`), `about` holds the why and the pledge, and `roadmap` the devices that come next. A feature gets its block in `features` and its paragraph in the guide that owns the subject, and the paragraph's `h3` carries the anchor the block links to. The words the layout itself speaks (nav, footer, the call to action that closes a section, `shared/_cta.html.erb`) live in `config/locales/`.

The legal notice (`legal-notice.*`, `/fr/mentions-legales`) names the hosting provider only, as article 1-1 II of the [LCEN](https://www.legifrance.gouv.fr/loda/id/JORFTEXT000000801164) allows a site published non-professionally, provided the host holds the publisher's identity. It reads the host from the credentials, not from the source, added to production with `mise run credentials:production`. Development has them already. Without them, as in CI, which has no key, the fields render empty:

```yaml
legal:
  host: [hosting provider]
  host_address: [hosting provider address]
```

Images live in `content_images/pages/<slug>/` and are drawn with `pages_image_tag`. The device screens under `pages/skyblip-go/` are drawn with `screen_image_tag` from whichever page shows them, and they are not artwork: each is the 200x200 framebuffer of the WASM simulator, driven to that state and read out of `simulator_fb()`, so a page that changes on the device is a capture that has to be taken again. They are stored at 200x200 in two colours and scaled by an integer factor on the page (`features.css`), because any other factor resamples a panel pixel.

Every other image, a photo or an app screenshot, is AVIF, encoded by libvips from a lossless source: `vips heifsave in.png out.avif --Q 75 --compression av1 --effort 4 --strip --subsample-mode auto` for a photo, `--Q 70 --subsample-mode off` for a screenshot, whose text loses its edges to chroma subsampling. Rails registers no AVIF type, so `config/initializers/mime_types.rb` does, or the dev server sends the files without a Content-Type.

The legend under `pages/skyblip-go/blips/` is the other half of the same rule: one PNG per traffic blip, each drawn on its own 19x25 canvas by `ui::draw_blip`, the widget the radar page itself calls, so the table on the page cannot drift from the glass. `bin/blips.cpp` is that dumper, a host binary linked against `ui/canvas.cpp` and `ui/widgets/blip.cpp` and nothing else.

Taking them again is `bin/screenshots`: it builds the firmware's WASM, flies one device per scene, compiles the dumper, and writes the PNGs over the ones in the tree. A scene names the page, the menu and the alarm level it expects and the script stops on the first one that does not hold, because a capture of the wrong glass is worse than a stale one. The page ids and the menu rows are read out of `pages/page.h` and `pages/menu.cpp` rather than copied into the script, so a scene opens `NEARBY_MENU.SelfTest` by name: the copy that used to sit here went stale when a page was inserted, the self-test scene opened the row below and the check passed on the wrong glass. The scenes are the file: to change what a screen shows, change the sky it is flown in. The radar is captured both ways for `displays`, a `-to-scale` shot beside the one of the same sky on `ALL`, which is what a fresh device boots on, so every other radar capture is `ALL` too. `PLOT` is pressed through the menu like any other row. Node runs the same `embed.js` a browser does, so the only thing it hands Emscripten is `instantiateWasm`, which is how the module is loaded off the disk rather than fetched.

Look and layout come from the token sets in `app/assets/stylesheets/`. Colors, spacing and type are CSS variables in `_global.css`; components never hardcode a value.

The palette is the wordmark's: `--ink` for text and the footer, `--paper` for the page, `--sky`, the grey-blue of "sky", and `--blip`, the orange of the signal. Each of the last two has a soft tint for a band (`--sky-soft`, `--blip-soft`) and a deep shade for text (`--sky-deep`, `--blip-text`). A page alternates paper and sky bands, keeps one blip band for its closing message, and ends on the ink footer.

The ownship editor, `/briefing/ownship`, draws the symbol at the centre of the radar, one per aircraft type. The drawings and the outlines they are made against are files in `content/ownships/`, beside a README on what a drawing must be, where each outline comes from, why the glider is a JS3, and the Bluetooth command a device could take one in. The editing runs in the browser, in `ownship_controller.js` over `#skyblip/ownship`, and sends nothing to the server: the page is loaded once per aircraft type, and a drawing travels as text.

The simulated device case is `app/assets/stylesheets/device.css`, shared by the public simulator and the editable simulator under `/briefing/simulator`.

The update page (`manage.*`) installs firmware and edits device settings using `app/javascript/skyblip/`, loaded through Importmap. The Bluetooth transport and update dialect are described in that folder's README. Every refusal the device can answer is a row in the page's tables, marked `data-manage-word`, and the controller reads the sentence out of the row. `test/javascript/test_words.mjs` fails when `firmware/core/comms/config.cpp` gains a refusal that either language has no row for.

## Firmware and simulator releases

GitHub Releases hold firmware and the simulator runtime. Loading an app page never waits for GitHub. A separate eager Turbo Frame requests the shelf or simulator build from Rails, which lists the published releases live. The shelf offers the newest stable release and kept development builds. The simulator uses the newest stable release when it carries the simulator files, and the newest main development build otherwise. It never chooses a feature-branch simulator automatically.

The browser fetches selected files through `/releases/:tag/:file`, because GitHub's download host does not allow browser CORS requests. The endpoint accepts only our repository, recognized release tags, firmware filenames and the four simulator filenames. Redirects are limited to GitHub's asset hosts, downloads are size-bounded, and successful immutable responses are browser-cacheable. The server does not retain release files or metadata between requests.

Release type selects the offered channel, not proof of a signing key. The update client reads the chosen binary and checks its actual key, version and sensor-hub requirements before uploading. MCUboot independently verifies its signature. Production release checksums and provenance stay published on GitHub. The server does not add attestation verification.

A GitHub failure is contained in the frame. The update page reports the unavailable list and keeps its file picker. The simulator reports an unavailable build instead of blocking the page. Requests start without a GitHub token, sharing the unauthenticated limit of 60 API calls per hour on the crooz IP. #132 tracks adding a token. #131 tracks Litestream and kamal-backup before durable device registrations.

`firmware.yml` and `release.yml` publish `skyblip_simulator.js`, `skyblip_simulator.wasm`, `embed.js` and `scenarios.json` with firmware images. Older releases without those files cannot run in the simulator. Publishing or pruning firmware never deploys Rails. Changing app JS, CSS or content does.

## Building and deploying

Deployments use `.github/workflows/server.yml` and Kamal on the existing Debian crooz host. Setup, production credentials, the remote builder and the one-time DNS cutover are documented in [DEPLOYMENT.md](DEPLOYMENT.md). No C++ or Emscripten toolchain is needed in the Rails image.

## License

MIT, see [`LICENSE`](LICENSE), the same license as the repository root. Only `firmware/` is GPL-3.0-only.
