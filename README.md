# RenderOJN #
Renders O2Jam OJN/OJM to MP3/WAV/OGG music file

- **Author**: Yana Nugraha
- **Author Homepage**: http://djzmo.com
- **Latest Version**: 1.0.3

--------------------------------------------------------------------------------------------------

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

## Summary ##

RenderOJN is a utility which is able to render an OJN file into PCM and encode to
preferred audio file format.

See it in action: https://www.youtube.com/watch?v=snYnd_IvmbM

## Usage Notes ##

- In realtime mode, it is HIGHLY recommended to do nothing with your computer
  while rendering a keysounded music to preserve output quality.
- In quick mode, each trigger is placed at its exact target-frame offset, so
  keysounded music no longer loses timing accuracy the way it did before v1.0.0.

## Version History ##

### v1.0.3
- Korean and Japanese titles, artists and charter names are decoded from the
  OJN's CP949 text and written to the MP3/Ogg tags as UTF-8, so they no longer
  come out as mojibake. Western-accented (Latin-1) titles are preserved as
  before rather than being mangled. The web app shows and names files by the
  real title.
- A folder input no longer lets two charts with the same title overwrite each
  other; the second gets a ` (2)` suffix.
- Windows builds accept non-ASCII input paths and `--outfile` names, which
  previously failed with "Unable to create temporary output".
- Default output name is now `<input stem>.<format>` (`o2ma100.mp3`, not
  `o2ma100.ojn.mp3`), and `--tracks keysounds` / `--tracks background` add
  `_keysounds` / `_background` so a stem render never overwrites the full mix.
  The web app names its downloads the same way.
- Added `--title-as-filename` to name the output after the chart's title, and
  `--outdir` to choose the folder.
- A folder can be given instead of a file: every `.ojn` in it is rendered into
  `<folder>/render` (or `--outdir`), one after another, with a summary at the
  end.
- The chart's cover art is embedded in MP3 (ID3v2 APIC) and Ogg
  (METADATA_BLOCK_PICTURE) output; `--no-cover-art` leaves it out.
- The web app's format, bitrate and tracks controls now show the active
  choice clearly.

### v1.0.2
- Added `--tracks` to render one note role in isolation: `keysounds` for the
  playable lanes, `background` for the autoplay/BGM stream, or `all` (the
  default). All three keep the chart's original length. The web app exposes the
  same choice as a Tracks control.

### v1.0.1
- Supports Korea-era encrypted `new` OJN wrappers, which previously could not
  be opened at all. They are decrypted transparently, so no extra option is
  needed.

### v1.0.0
- Rewritten on CMake and vcpkg; FMOD and Boost are no longer used.
- Portable archives for Windows and Linux, with no runtime DLLs to ship.
- Correct timing for fractional measures, mid-measure BPM changes, and
  subdivisions that do not divide 192.
- Output length now follows chart content, so late notes are no longer clipped.
- Supports M30 flags 0/16/32, and sparse OMC/OJM directory slots.
- Added `--sample-package` to override sample package lookup.

### v0.8.2
Released: June 2012
- Able to encode to OGG with automatic tagging.
- Added output quality option.
- Added "Genre" field to the music tag.
- Added application icon.
- Added progress percentage on time-consuming processes.
- Fixed chopped music ending bug.

### v0.8.0
- Able to encode to MP3 with automatic ID3 tagging.
- Keysounded notes might or might not be rendered properly in quick mode.

## Usage ##

Usage: ```RenderOJN [inputfile [options]]```

Rendering Options:

```
  --rendermode <mode>       Rendering Mode (quick, realtime). Default: quick
  --tracks <selection>      Which notes to sound (all, keysounds, background).
                            Default: all. keysounds are the playable lanes;
                            background is the autoplay/BGM stream. All three
                            keep the same length. Background fidelity varies by
                            chart: O2Jam charts share samples between roles, so
                            a background render is not a mastered instrumental.
  --format <format>         Output Format (wav, mp3, ogg). Default: mp3
  --outfile <filename>      Output Filename. Default: <input stem>.<format>,
                            with _keysounds or _background appended for a
                            --tracks stem. The extension is optional and
                            follows --format.
  --outdir <folder>         Folder for the output. Default: beside the input
                            (or <folder>/render for a folder input)
  --title-as-filename       Name the output after the chart's title rather
                            than the input file
  --no-cover-art            Do not embed the chart's cover art in MP3/Ogg tags
  --quality <quality>       Output Quality (for mp3 and ogg). Default: 3
                            3 - Best, 2 - Standard, 1 - Poor
```

Misc. Options:

```
  --difficulty <difficulty> Note Difficulty (e, n, h). Default: h
  --sample-package <path>   Sample package to use instead of the one named
                            in the OJN
  --play                    Play the music instead of generating an output file
  --help                    Display this text
```

Example:

```
RenderOJN o2ma100.ojn --outfile BachAlive.mp3 --quality 2
RenderOJN o2ma100.ojn --rendermode realtime --format wav
RenderOJN o2ma100.ojn --tracks keysounds --format wav
RenderOJN o2ma100.ojn --tracks background         (writes o2ma100_background.mp3)
RenderOJN o2ma100.ojn --title-as-filename         (writes Bach Alive.mp3)
RenderOJN "C:\O2Jam\Music" --title-as-filename    (renders every .ojn into C:\O2Jam\Music\render)
RenderOJN o2ma100.ojn --play
RenderOJN --help
```

Values are lowercase and case-sensitive. Success exits 0, runtime and input
errors exit 1, and usage errors exit 2. A folder input renders each `.ojn` in
turn, reports any that fail, and exits 1 if one did. Output is written to a temporary file
first and published only after rendering, encoding, and tagging all succeed, so
a failed run never leaves a partial file behind.

Sample-package lookup is: `--sample-package`, then the package named in the OJN
beside that OJN, then the current directory (with a warning). Missing or
unsupported encoded samples fail explicitly rather than being silently skipped.

## Compatibility ##

Supports ordinary OJN wrappers, Korea-era encrypted `new` wrappers, and M30
packages using flag 0 (plaintext), flag 16 (`nami`), or flag 32 (plaintext),
with bounded OMC/OJM parsing. Every M30 payload must decode to a nonempty Ogg
stream, and only codec codes 0 and 5 are accepted. Flags 0 and 16 are
corpus-proven; no real flag-32 package was found, so that variant rests on the
CXO2 implementation and synthetic tests instead.

`new` wrappers are decrypted transparently — you pass the file exactly as you
would an ordinary one. All 958 charts across three retail installations parse,
decode, and render.

Both rendering modes use deterministic float32 stereo mixing at 48 kHz, a
100-voice cap, note type-3 skip, and note-type-4 `RefID + 1000` mapping. Quick
mode is unpaced and processes 1,024-frame blocks, placing each trigger at its
exact intra-block offset; realtime uses 48-frame scheduling with wall-clock
pacing.

See [docs/compatibility.md](docs/compatibility.md) for the full scope.

## Build ##

Requirements: CMake 3.25+, a C++17 compiler, and vcpkg manifest mode. Set
`VCPKG_ROOT`, then configure a platform preset:

```
cmake --preset windows-x64
cmake --build --preset windows-x64-release
ctest --preset windows-x64-test
cmake --build out/build/windows-x64 --target renderojn_package --config Release
```

Presets cover Windows x64, Windows x86 legacy comparison, Linux x64, Linux
ASan, Linux Clang/libFuzzer, and macOS universal. Note that a preset existing
is not the same as that platform being verified: Windows x64 and Linux x64
(including the manylinux2014/glibc-2.17 release container) are built and
tested, while **macOS universal and the Windows x86 legacy comparison build are
untested** because no such machine was available. See
[docs/release.md](docs/release.md).

The repository includes only generated and synthetic test fixtures. Place
private assets under `tests/fixtures/private/` (ignored) and use
`tools/CaptureLegacy.ps1` to record legacy behaviour for comparison without
modifying the original inputs.

The original pre-1.0.0 source is retained under `src/Nx/` for comparison during
migration. It is not part of the CMake targets.

## Dependencies ##

- [libsndfile](https://github.com/erikd/libsndfile)
- [taglib](https://github.com/taglib/taglib)
- [LAME](https://lame.sourceforge.io/)
- [libvorbis](https://xiph.org/vorbis/)
- [miniaudio](https://github.com/mackron/miniaudio)
- [Catch2](https://github.com/catchorg/Catch2) for tests

### License ###

This is an open-sourced software licensed under the [GNU GPL v3 license](http://www.gnu.org/licenses/gpl-3.0.en.html).
