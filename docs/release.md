# Release procedure

Use a clean checkout with the pinned vcpkg baseline.  Build release presets and
run matching test presets before packaging:

```text
windows-x64       -> RenderOJN-1.0.0-windows-x64.zip
linux-x64         -> RenderOJN-1.0.0-linux-x86_64-glibc2.17.tar.gz
macos-universal   -> RenderOJN-1.0.0-macos-universal.zip
```

Verification status for 1.0.0: `windows-x64` and `linux-x64` are built and
tested, the latter including the manylinux2014/glibc-2.17 container with a
clean ASan/UBSan run and a checksum-verified archive. **`macos-universal` is
untested**, as is the Windows x86 legacy comparison build — no machine was
available for either, so neither has been built or tested for this release.
The macOS steps below are the intended procedure, not a record of a completed
run; do not treat the macOS archive as verified until it has actually been
produced and its Mach-O slices inspected.

Build Linux release artifacts inside the manylinux2014/glibc-2.17 environment.
On Windows, Docker Desktop with WSL 2 integration enabled can run the same
release flow from an Ubuntu WSL shell:

```text
docker build -t renderojn-manylinux2014 -f cmake/Dockerfile.manylinux2014 .
docker run --rm -e CMAKE_BUILD_PARALLEL_LEVEL=2 -v "$PWD:/src" renderojn-manylinux2014 bash /src/cmake/build-manylinux2014.sh
```

The resulting Linux archive is written to
`out/build/linux-x64-manylinux2014/`. Do not certify a glibc-2.17 release from
an ordinary distribution build, even when its tests pass.

For parser-fuzz smoke coverage, configure `linux-x64-fuzz` with Clang and run
the two `renderojn_fuzz_*` targets against `fuzz/corpus/`.

Inspect both Mach-O slices for `macos-universal`; macOS deployment target is
10.15 for Intel and 11.0 for arm64.  Place `README.md`, `LICENSE`, and
`THIRD_PARTY_NOTICES.md` beside the executable.  Generate a `SHA256SUMS` file
for the three archives.  Do not include FMOD or legacy DLLs.

Release gates: all unit/integration/fuzz-smoke tests; no sanitizer failure;
package smoke tests; private-fixture parity with timing/onset/audio/tag checks;
and manual test of a real output device.  A missing private asset or platform VM
blocks only that release gate, never the synthetic safety coverage claim.
