# .deepsec

The configuration of [deepsec](https://deepsec.sh), the security review the `secure-skyblip` skill runs every day (`.agents/skills/secure-skyblip/SKILL.md`). It reviews two projects, `skyblip-server` and `skyblip-firmware`, through pi on the maintainer's Anthropic login.

| File | What it holds |
|---|---|
| `deepsec.config.ts` | the projects, the model, and where the state lives |
| `server.md`, `firmware.md` | the context deepsec puts in every prompt: what the code does, its trust shape, its threat model and its known false positives. Keep each under 100 lines |
| `matchers/` | the regex matchers that pick which files the agents read. deepsec's own reach 6% of the server's Ruby and none of the C++, so these name the controllers, the trust-path models and the firmware's input doors |

The state, which files were read, the findings and their verdicts, lives in `~/.local/share/skyblip-deepsec/`, outside every worktree, so each day's run builds on the last. `DEEPSEC_DATA_ROOT` moves it.

```sh
npm ci
npx deepsec scan --project-id skyblip-server --root "$PWD/../server"
git -C ../server diff --name-only --relative origin/main~10..origin/main | npx deepsec process --project-id skyblip-server --root "$PWD/../server" --files-from -
npx deepsec revalidate --project-id skyblip-server
npx deepsec export --project-id skyblip-server --format md-dir --out findings/server
```
