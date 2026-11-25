# Extending skyBlip

The firmware takes contributions in a few named places. Each place has a contract, written in its README, and a check that holds a contribution to it in CI. The maintainer reviews and merges every pull request.

A contribution lives in this repository and builds into the one image skyBlip signs. Nothing loads at runtime. Only an image signed with skyBlip's production key updates a unit (`keys/README.md`), and a declaration of conformity names a firmware version, so code outside the tree would fly uncovered. A fork that wants its own image builds and signs it under its own name (`README.md`, Before you ship a copy).

## Where a contribution goes

| Contribution | Where | Contract | Held by | On air |
|---|---|---|---|---|
| A discipline pack | `disciplines/<pack>/` | `disciplines/README.md`: it may raise a target to an advisory, never lower one | `scripts/check_discipline_boundary.py`, and the clamp in `traffic::AlarmTracker::update` | never |
| A radio protocol stack | `protocols/<stack>/` | `protocols/README.md`: plain types, one `receive()`, a `version.h`, its own tests | `scripts/check_protocol_boundary.py`, `make test-protocols`, the freeze locks | yes, so the maintainer decides every rule |
| A traffic source | `core/protocol/<source>_observation.*` and `model::Source` | `core/traffic/README.md`, Whichever system heard it: it writes `flight_state`, `Unknown` when its wire has none | `test/core/test_traffic_sources.cpp` | receives only |
| A part | `hardware/parts/<part>/` | `hardware/README.md`: a driver that fills a `ports/` role, and a model of the part | `scripts/check_test_coverage.py`, which asks for `test/hardware/test_<part>.cpp` | depends on the part |
| A board | `boards/<vendor>/<board>/` | `hardware/README.md`: a composition over a platform, which `hardware/platform/contract.h` asserts at compile time | `test/boards/` | no |
| A page | `products/<product>/pages/` | `products/skyblip_go/pages/README.md`: a pure function from a snapshot to pixels | the host suite | no |

A contribution that fits none of these starts as an issue, so the maintainer can open the place it needs before any code is written.

## What the maintainer keeps

The maintainer writes or rewrites every change to what the unit transmits, what makes it quieter and what every service runs on:

- what goes on air: the rules of each stack in `protocols/`, `core/radio/`, `core/timing/` and the encoders in `core/protocol/`;
- what silences an alarm: the ring and the tracker in `core/traffic/alarm.*`, `core/traffic/formation.*` and dismissal;
- the dispatcher in `disciplines/`, which decides which category flies which discipline;
- `runtime/`, the settings, the companion link's contract (`../schemas/link.v1.json`), `keys/` and the release scripts.

## A pull request

1. Fork the repository and branch off `main` as `<type>-<slug>`, such as `feat-soaring-circling-prediction`.
2. Write Conventional Commits, typed `feat`, `fix`, `ui`, `refactor`, `test`, `docs`, `perf`, `infra` or `deps`.
3. Name each test case as a claim, `subject: what holds` (`README.md`, Naming a test case).
4. Run `make test` and `make tidy` here. Then regenerate the generated pages from the repository root, `python3 scripts/behavior_index.py` and `python3 scripts/tuning_index.py`, and commit them with the change.
5. Open the pull request against `main`. CI signs its image with a key made for the run, so the image it builds updates no unit.

## License

skyBlip is MIT (`../LICENSE`), and a pull request submits its contribution under MIT.
