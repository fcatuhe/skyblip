# website

[skyblip.eu](https://skyblip.eu). A Rails app that never serves a request in production: [Parklife](https://github.com/benpickles/parklife) crawls it and writes static files, which GitHub Pages serves.

Every command here runs from this directory, not from the repo root.

## Running it

```sh
bin/setup          # gems, then bin/dev
bin/dev            # http://skyblip.localhost:8119
bin/ci             # what CI runs: rubocop, bundler-audit, importmap audit, brakeman
bin/screenshots    # retake the device screens off the firmware next door
```

## Content

Pages are files, not database rows: `content/pages/<slug>.html.erb`, with YAML frontmatter for `title`, `nav_title`, `description` and an optional localized `slug`. A page is bilingual by convention, `<slug>.html.erb` and `<slug>.fr.html.erb`, and the locale switcher offers a language only where the twin exists.

Parklife discovers pages by crawling from the root, so **a page nothing links to is not built**. Link it from the nav (`app/views/layouts/_nav.html.erb`) or from another page's body.

Images live in `content_images/pages/<slug>/` and are drawn with `pages_image_tag`. The device screens under `pages/skyblip-go/` are not artwork: each is the 200x200 framebuffer of the WASM simulator, driven to that state and read out of `simulator_fb()`, so a page that changes on the device is a capture that has to be taken again. They are stored at 200x200 in two colours and scaled by an integer factor on the page (`features.css`), because any other factor resamples a panel pixel.

The legend under `pages/skyblip-go/blips/` is the other half of the same rule: one PNG per traffic blip, each drawn on its own 19x25 canvas by `ui::draw_blip`, the widget the radar page itself calls, so the table on the page cannot drift from the glass. `bin/blips.cpp` is that dumper, a host binary linked against `ui/canvas.cpp` and `ui/widgets/blip.cpp` and nothing else.

Taking them again is `bin/screenshots`: it builds the firmware's WASM, flies one device per scene, compiles the dumper, and writes the PNGs over the ones in the tree. A scene names the page, the menu and the alarm level it expects and the script stops on the first one that does not hold, because a capture of the wrong glass is worse than a stale one. The page ids and the menu rows are read out of `pages/page.h` and `pages/menu.cpp` rather than copied into the script, so a scene opens `NEARBY_MENU.SelfTest` by name: the copy that used to sit here went stale when a page was inserted, the self-test scene opened the row below and the check passed on the wrong glass. The scenes are the file: to change what a screen shows, change the sky it is flown in. Node runs the same `embed.js` a browser does, so the only thing it hands Emscripten is `instantiateWasm`, which is how the module is loaded off the disk rather than fetched.

Look and layout come from the token sets in `app/assets/stylesheets/`. Colors, spacing and type are CSS variables in `_global.css`; components never hardcode a value.

The one exception is the simulated device. Its case is `simulator/device.css` at the repo root, drawn once for the development harness and for this site, and `bin/simulator-build` copies it into `public/simulator/<commit>/` beside the WASM. The page links it directly, so it is unlayered CSS and the few rules here that override it are unlayered too.

## Building and deploying

```sh
bin/static-build --base https://skyblip.eu   # -> build/
```

`bin/static-build` precompiles assets, runs Parklife, then copies `public/` over the result, which is how anything that must not be fingerprinted gets served verbatim.

Deployment is `.github/workflows/website.yml` at the repo root: a push to `main` that touches `website/`, `firmware/` or `simulator/` (the page embeds the firmware as WASM, in the simulator's case) builds and publishes to Pages. Nothing else triggers a deployment, and `workflow_dispatch` forces one.

## License

MIT, see [`LICENSE`](LICENSE), the same license as the repository root. Only `firmware/` is GPL-3.0-only.
