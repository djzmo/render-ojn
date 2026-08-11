# WASM toolchain probe

Verifies that the audio dependency stack works under Emscripten before any of
the web renderer is built on top of it. Proves, in one run:

- libsndfile can **write** WAV and Ogg/Vorbis through an in-memory
  `SF_VIRTUAL_IO` sink (no filesystem), and both decode back with the expected
  subtype at 2ch / 48 kHz.
- mp3lame can encode MP3 to a memory buffer.
- TagLib can tag MP3 and Ogg buffers via `ByteVectorStream`, with the title
  re-read from the rewritten bytes to prove the tag is really there.

It is a build-gate check, not a unit test, so it is not wired into CTest.

## Running it

Requires emsdk on `EMSDK`, and the dependencies installed for the
`wasm32-emscripten` triplet. The `ports/` overlay is **required** — stock
mp3lame does not configure for emscripten (see below).

```bash
vcpkg install libsndfile mp3lame taglib \
  --triplet wasm32-emscripten \
  --overlay-ports=./ports

cmake -S tools/wasm-probe -B out/build/wasm-probe -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_MANIFEST_MODE=OFF

cmake --build out/build/wasm-probe
node out/build/wasm-probe/probe.js
```

Expected final line: `STEP0 OK` (exit 0). Anything else prints the failing
stage.

## Two things that will waste your time otherwise

**`-sSTACK_SIZE` is mandatory.** libvorbis's analysis path
(`_preextrapolate_helper` → `vorbis_analysis_wrote`) overruns Emscripten's 64KB
default stack. Native builds get 1–8MB from the OS and never hit it. Without the
flag the probe dies inside the Ogg encode with a bare
`RuntimeError: memory access out of bounds`, which looks exactly like a pointer
bug in the virtual-IO callbacks and is not. Rebuild with
`-fsanitize=address -sASSERTIONS=2` and the real cause is named immediately.

**Stock mp3lame does not build for emscripten.** LAME 3.100 vendors
`config.sub`/`config.guess` from 2015, predating emscripten's entry in the
autotools system list, so configure rejects the host triplet with
``Invalid configuration `wasm32-unknown-emscripten'`` before compiling anything.
`ports/mp3lame/` is an overlay that refreshes both scripts; it is identical to
upstream on every other triplet.

**Also note:** a failing `vcpkg install` was observed exiting **0**, with the
error only in its log body. Assert on installed artifacts or grep for
`BUILD_FAILED` — do not trust the exit status.
