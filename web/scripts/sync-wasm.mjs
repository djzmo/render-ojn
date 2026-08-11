/**
 * Copies the Emscripten build output into src/wasm/ so Vite can bundle it.
 *
 * The module is a build artifact, not source, so it is git-ignored and staged
 * here rather than committed. Run after `cmake --build out/build/wasm`.
 */
import { copyFileSync, existsSync, mkdirSync } from "node:fs"
import path from "node:path"
import { fileURLToPath } from "node:url"

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, "..", "..")
const source = process.env.RENDEROJN_WASM_DIR
  ? path.resolve(process.env.RENDEROJN_WASM_DIR)
  : path.join(repoRoot, "out", "build", "wasm")
const destination = path.join(here, "..", "src", "wasm")

const files = ["renderojn.js", "renderojn.wasm"]
const missing = files.filter((name) => !existsSync(path.join(source, name)))

if (missing.length) {
  console.error(`Missing WASM build output in ${source}: ${missing.join(", ")}`)
  console.error("Build it first — see tools/wasm-verify/README.md.")
  process.exit(1)
}

mkdirSync(destination, { recursive: true })
for (const name of files) {
  copyFileSync(path.join(source, name), path.join(destination, name))
  console.log(`synced ${name}`)
}
