# WASM render verification

Renders a chart through the WebAssembly module headlessly, using the same
`renderojn.js` the browser loads. Use it to confirm the web build still agrees
with the CLI after a change to the core.

## Building the module

```bash
cmake -S . -B out/build/wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_MANIFEST_MODE=OFF \
  -DRENDEROJN_BUILD_TESTS=OFF

cmake --build out/build/wasm
```

Dependencies must be installed for `wasm32-emscripten` with the `ports/`
overlay — stock mp3lame does not configure for emscripten. See
[../wasm-probe/README.md](../wasm-probe/README.md).

## Running

```bash
node tools/wasm-verify/verify.mjs <chart.ojn> <package.ojm> [0|1|2] [wav|mp3|ogg] [outdir]
```

Prints title, artist, charter, package name, per-difficulty note counts, any
warnings the core raised, the SHA-256 of the encoded output, and writes the
audio beside it. Set `RENDEROJN_WASM` to use a module outside
`out/build/wasm/renderojn.js`.

## Comparing against the CLI

```bash
RenderOJN chart.ojn --format wav --difficulty h --outfile cli.wav
node tools/wasm-verify/verify.mjs chart.ojn chart.ojm 2 wav .
```

**Do not expect identical hashes.** WASM and the MSVC CLI are sample-exact to
within one LSB, not bit-identical. Measured on `o2ma100` (WAV, hard):

| | |
|---|---|
| Size and header | identical |
| Differing samples | 22,239 of 9,984,000 (0.22%) |
| Max deviation | ±1 of 32767 |
| Direction | 11,171 high / 11,068 low |

Every difference is exactly one least-significant bit, and they cancel in both
directions. `std::lround(clipped * 32767.0F)` (`core/output/Encoder.cpp`) breaks
`.5` ties differently on MSVC x64 than on LLVM/wasm. A genuine mixing defect
would show large, directional errors instead — so **compare sample deltas, not
hashes**:

```js
const A = new Int16Array(cli.buffer, cli.byteOffset + 44, (cli.length - 44) >> 1);
const B = new Int16Array(wasm.buffer, wasm.byteOffset + 44, (wasm.length - 44) >> 1);
let max = 0;
for (let i = 0; i < A.length; i++) max = Math.max(max, Math.abs(A[i] - B[i]));
// max must be <= 1
```

A `max` above 1, a size mismatch, or a missing/extra compatibility warning is a
real regression.

## What a good run looks like

`o2ma121` is the useful case: its compatibility profile is keyed on the SHA-256
of *both* input files, so the correction only fires if the bytes reached the
module intact.

```
warning:  applied o2ma121 OMC background timing compatibility correction (+2293 frames)
```

If that line disappears, the byte transport into WASM has broken — check that
callers pass a `Uint8Array` and not a string. embind marshals `std::string` as
UTF-8 and silently corrupts any byte above 0x7F.

Note that Latin-1 header text (for example the charter `Hiro+SRS(¡Ü)`) arrives
as codepoints matching the file's raw bytes. That is correct; a terminal set to
UTF-8 may render it as mojibake even though the data is intact.
