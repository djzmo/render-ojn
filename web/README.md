# RenderOJN Web

Browser UI for converting O2Jam charts to audio. The renderer is the project's
C++ core compiled to WebAssembly, so nothing is uploaded anywhere.

## Building

The app renders through the real C++ core compiled to WebAssembly, so the WASM
module must exist before the web build. Build it once:

```bash
cmake --build out/build/wasm
```

(See [`tools/wasm-verify/README.md`](../tools/wasm-verify/README.md) for the
configure step and its two prerequisites — the `ports/` overlay and emsdk.)

Then:

```bash
npm install
npm run build   # syncs the module into src/wasm/, then builds
npm run dev
```

`npm run build` runs `scripts/sync-wasm.mjs` first, copying `renderojn.js` and
`renderojn.wasm` out of the CMake build tree. They are build artifacts, so they
are git-ignored rather than committed; `npm run sync-wasm` re-copies them after
a C++ change. Point `RENDEROJN_WASM_DIR` elsewhere to use a different build.

`vite.config.ts` sets `base: '/render-ojn/'` for GitHub Pages project-page
hosting, so the dev server serves at `/render-ojn/`, not `/`.

### Developing without the toolchain

`VITE_RENDEROJN_STUB=1 npm run dev` swaps in [`src/lib/stub.ts`](src/lib/stub.ts),
a deterministic fake that needs no Emscripten build. Useful for UI work; it
invents titles and emits silent audio, so never use it to check output.

## How the renderer is wired

[`src/lib/renderojn.ts`](src/lib/renderojn.ts) declares the contract and is the
only file that knows which implementation is live:

```ts
readOjnInfo(bytes: Uint8Array): Promise<OjnInfo>
render(ojn, ojm, difficulty, format, quality, onProgress): Promise<RenderResult>
```

The real implementation runs in a Web Worker
([`src/workers/render.worker.ts`](src/workers/render.worker.ts)), with
[`src/lib/worker-client.ts`](src/lib/worker-client.ts) as the main-thread half.
The mix is single-threaded and synchronous — GitHub Pages cannot send the
COOP/COEP headers that WASM threads need (§7) — so the worker is what keeps the
page responsive rather than what makes the render fast.

Two boundary details worth knowing:

- **Bytes cross as `Uint8Array`, never `string`.** embind marshals `std::string`
  as UTF-8 and silently corrupts any byte above `0x7F`, which destroys binary
  input. The worker client copies before transferring, because a transferred
  buffer is detached on this side and the queue keeps the original bytes to
  re-render at another difficulty or format.
- **Format is mapped through `FORMAT_VALUES`, not array position.**
  `OUTPUT_FORMATS` is display order (`wav, ogg, mp3`); the C++ enum is
  `{Wav = 0, Mp3 = 1, Ogg = 2}`. An index-based mapping would hand OGG the value
  that means MP3 and produce an MP3 body in a `.ogg` file, with no error.

`render()` returns the core's diagnostics alongside the bytes — compatibility
corrections, ignored directory records — and the row displays them, so a web
render is no less informative than the CLI's stderr.

## Tests

```bash
npm test              # once
npm run test:watch    # while working
npm run test:coverage
```

104 tests across `tests/`, run by Vitest. They deliberately need **no C++
build**: nothing imports the worker module or the Emscripten artifact, so the
suite passes on a fresh clone and runs in CI without emsdk or vcpkg. The
quickest way to confirm that still holds is to move `src/wasm/` aside and run
`npm test`.

Three of them exist because of bugs that shipped and were caught by hand:

| Test | Bug it pins |
|---|---|
| `a second drop keeps the skipped filenames from the first` | `setRejected` replaced the list, so earlier skipped files looked accepted |
| `renderAll picks up a chart that becomes ready during the batch` | `renderAll` iterated a stale snapshot |
| `quality values are the CLI's 1-3 scale and never include zero` | UI used a 0-based scale, silently rendering one bitrate tier low |

Notes on the harness, all learned the hard way:

- **`act(async () => ...)` must always be awaited.** An un-awaited one
  interleaves act scopes; the symptom is every *later* test in the file failing
  with a null `result.current`, which looks like a mount problem and is not.
- **`vi.restoreAllMocks()` is not used in `use-queue.test.ts`.** It puts back
  jsdom's own `URL.createObjectURL`, which is unimplemented and throws.
- **`globals: true`** is set so `@testing-library/react` registers its
  auto-cleanup; without it each test's hook stays mounted into the next.
- `zip.test.ts` checks output against **fflate** rather than a hand-written
  decoder — verifying one ZIP implementation with a second built from the same
  assumptions lets a shared misreading cancel out. CRCs are checked against
  Node's `zlib.crc32`.
- `worker-client.test.ts` needs only a fake `Worker` class. The worker module is
  imported as `import type`, so under `verbatimModuleSyntax` it is erased at
  compile time and no WASM is ever loaded.

## Structure

| Path | Role |
|---|---|
| `src/lib/renderojn.ts` | Module contract and shared domain types |
| `src/lib/worker-client.ts` | Main-thread half of the worker protocol |
| `src/workers/render.worker.ts` | Owns the WASM instance; runs the mix |
| `src/lib/stub.ts` | Opt-in fake for toolchain-free UI work |
| `src/lib/pairing.ts` | OJN↔package matching, formatting helpers |
| `src/hooks/use-queue.ts` | Ingest → pair → render lifecycle |
| `src/components/dropzone.tsx` | Multi-file and directory intake |
| `src/components/chart-row.tsx` | One queue row, all five states |
| `src/components/render-queue.tsx` | Queue shell, format and quality controls |

### Pairing is derived, not stored

A chart is paired iff some dropped package matches the name its header states.
`reconcile()` recomputes that from both lists on every render rather than
keeping a second copy in sync, which is what lets a package dropped *now* pair
with a chart whose header parses a moment *later* — in either order, with no
reconciliation pass. Packages nothing claims are held silently and may pair when
more files arrive.

## Design

The "sample bank" direction from §5: warm near-black substrate, phosphor amber
for ready states, desaturated red for a missing dependency. Every piece of
file-derived data is monospaced (`[data-file-data]`) — that is what separates
what the tool says from what the file says.

The restraint is deliberate. This is a tool for converting files in bulk, not a
landing page; the queue is the interface, and anything competing with it for
attention is working against the task. A loud, saturated marketing treatment was
tried here and reverted for exactly that reason.

The signature element is the unpaired row. Rather than a generic error it renders
as a visibly incomplete slot naming the exact file it needs (`Needs OZUKI.ojm`),
with a slow scanline that resolves in one animated transition when the package
arrives. Both animations are disabled under `prefers-reduced-motion`.

The app is dark-only by design, so `:root` and `.dark` carry the same values and
the theme class is pinned in `main.tsx`.

## Notes

- The build prints `Module "node:module" has been externalized for browser
  compatibility` for `src/wasm/renderojn.js`. Expected: the module is built with
  `-sENVIRONMENT=web,worker,node` so `tools/wasm-verify` can exercise the exact
  binary the browser loads. The Node branch sits behind a runtime environment
  check and never runs in a browser.
- `src/components/ui/progress.tsx` has one local change from upstream: the
  indicator is absolutely positioned. Base UI sets the fill as an inline
  `width: N%`, which collapses to zero as a static flex item inside the flex
  track.
- Per-file downloads, plus "Download all" once two or more renders have
  finished. `src/lib/zip.ts` writes the archive by hand with stored (not
  deflated) entries — MP3 and Ogg payloads are already compressed, so deflate
  would cost CPU on tens of megabytes for almost no gain, and a dependency for
  that trade is not worth it. It emits ZIP64 headers past the 4 GiB limits,
  which a queue of WAV renders reaches, and disambiguates duplicate filenames
  so two charts rendering to the same name do not overwrite each other.
