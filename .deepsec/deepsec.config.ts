import { readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { defineConfig, type DeepsecPlugin } from "deepsec/config";
import { serverController, serverTrustModel } from "./matchers/server.js";
import { firmwareDoor } from "./matchers/firmware.js";

// INFO: fc 25nov26 deepsec 2.3.16 ignores the documented dataDir field, so the state goes through DEEPSEC_DATA_ROOT, outside every worktree
process.env.DEEPSEC_DATA_ROOT ||= join(homedir(), ".local/share/skyblip-deepsec");

const info = (name: string) => readFileSync(new URL(`./${name}.md`, import.meta.url), "utf8");

const skyblipMatchers: DeepsecPlugin = {
  name: "skyblip-matchers",
  matchers: [serverController, serverTrustModel, firmwareDoor],
};

export default defineConfig({
  defaultAgent: "pi",
  defaultModel: "anthropic/claude-opus-5-5",
  defaultThinkingLevel: "high",
  projects: [
    { id: "skyblip-server", root: "../server", githubUrl: "https://github.com/fcatuhe/skyblip/blob/main/server", infoMarkdown: info("server") },
    { id: "skyblip-firmware", root: "../firmware", githubUrl: "https://github.com/fcatuhe/skyblip/blob/main/firmware", infoMarkdown: info("firmware") },
  ],
  plugins: [skyblipMatchers],
});
