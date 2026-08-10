# Compatibility baseline

RenderOJN preserves the legacy CLI defaults and note mapping for ordinary
OJN inputs.  It renders in float32 stereo at 48 kHz, ignores legacy chart
volume/pan, skips note type 3, and resolves note type 4 as `RefID + 1000`.

The OJN header's per-difficulty duration field is song-list display metadata,
not a bound on chart content, and it may under-report the real chart length:
30 of the 229 ordinary charts in the O2Jam and O2Jam Thai corpora place notes
after their declared duration, by up to 3.97 seconds (`o2ma105.ojn`). Neither
Open2Jam nor CXO2 validates events against this field — Open2Jam reads it into
a display getter and never consults it, and CXO2 does not read it at all.

Output therefore ends one second after the later of the declared duration and
the final note onset, so late notes are never clipped and the terminal second
still covers sample decay. Event times remain bounded by the absolute six-hour
limit; an event past that is malformed, as are non-finite, negative, or
overflowing times.

Supported package baseline: M30 flags 0, 16, and 32 plus bounded OMC/OJM record
parsing. OJN source subdivisions are exact, nonzero channel-0 measure fractions
and channel-1 BPM changes are normalized before frame conversion, and all
declared OJN event/note/measure/package counts are validated. Both timing
constructs occur in real charts: across the 958-chart corpus, 35 charts carry
325 channel-0 measure fractions (values 0.25/0.5/0.75, i.e. 1/4, 2/4 and 3/4
measures against 4/4) and 400 charts carry 21,460 channel-1 tempo events. No
real chart uses a subdivision that does not divide 192. Encrypted OMC PCM
samples use the format's 17-block rearrangement followed by ACCXOR decoding;
OJM PCM samples remain plaintext. Empty PCM and Ogg directory slots retain
their positional IDs, are ignored with a warning, and are never passed to a
decoder; complete trailing zero-byte Ogg directory slots are also accepted.

M30 payloads are Ogg streams. Flag 16 is obfuscated with a repeating `nami` XOR
over complete four-byte groups, leaving any trailing 0-3 byte partial group
verbatim, while flags 0 and 32 are plaintext and are preserved byte for byte.
Every decoded M30 payload must be nonempty and begin with `OggS`; a payload that
does not is rejected rather than passed to the decoder. Codec code 0 selects the
background bank and codec 5 the normal bank; every other codec code is rejected.

The evidence basis for the three M30 flags is not the same, and the difference
is worth stating precisely. Flags 0 and 16 are corpus-proven: a 980-package
sweep found 605 flag-0 and 346 flag-16 packages, all parsed and decoded. Every
installed flag-0 package is structurally complete and its declared samples
already begin with a plain `OggS` signature, so no decryption is required, and
CXO2's `M30Archive` transforms only flag 16 while passing other flags through
unmodified. **No flag-32 package appears anywhere in that corpus**, so flag 32
is supported as a plaintext variant on CXO2 source agreement plus synthetic
tests only; it is deliberately untested against real data.

Korea-era encrypted `new` OJN wrappers are supported as of 1.0.1. The container
holds an ordinary OJN behind a byte reversal and a repeating XOR key, with the
block size and the main, mid, and initial key bytes carried in the wrapper's own
8-byte header, so no key material is external to the file. The decrypted result
must carry the `ojn\0` signature or the file is rejected; a wrong key yields
plausible-looking bytes rather than an obvious failure, so that check is what
separates a genuine decrypt from garbage. Every one of the 494 `new` wrappers in
the reference corpus decrypts to a valid ordinary OJN, across the eight block
sizes those files use (4 through 11).

Any nonempty encoded sample that libsndfile cannot decode fails the run;
samples are never silently omitted.

## Reference corpus

The claims above were measured against three retail installations: O2Jam
(130 charts), O2Jam Thai (100), and NOWCOM O2Jam (728) — 958 charts and 980
sample packages. 464 are ordinary OJN charts and 494 are Korea-era `new`
wrappers; as of 1.0.1 both are in scope, and all 958 parse, decode, and render.
Package kinds are 605 M30 flag 0, 346 M30 flag 16, 13 OJM and 15 OMC. Every
`new` wrapper pairs with a package kind that was already supported, so
decryption alone brought those charts into range.

Timing constructs across all 958 charts: 35 charts carry 325 channel-0 measure
fractions and 400 charts carry 21,460 channel-1 tempo events. No chart uses a
subdivision that does not divide 192. The 1.0.0 figures were lower (19 and 186
charts) only because the `new`-wrapped charts could not be read at the time.

Note that the same chart id recurs across installations, so identical filenames
under different roots must be kept distinct when reporting per-case results.

Three specimens are worth recording, because each looks like a defect and only
one is:

`o2ma105.ojn` declares a 97-second hard duration while its last note falls at
roughly 101 seconds. It is well-formed; it is simply the most extreme case of
the declared-duration under-reporting described above, and it renders correctly
once output length follows content.

`o2ma848.ojm` is genuinely malformed and is rejected. Its payload-size field
holds the total file size rather than the payload size — off by exactly the
28-byte header — and its sample count declares 992 records where the directory
contains 363. The body is otherwise intact: all 363 records use codec 5, every
payload decodes to a valid `OggS` stream, and the directory ends exactly at
EOF. The rejection is nonetheless correct, because 951 of the 952 M30 packages
in the corpus use the opposite convention; relaxing either check to accommodate
one specimen would mean trusting corrupt count fields everywhere else. Both
conventions do coexist legitimately in this format — OMC validates against
total file size while M30 validates against payload size — which is the likely
origin of the discrepancy. As of 1.0.1 this is the single chart in the corpus
that cannot be rendered: its `new`-wrapped chart now decrypts and parses
normally, so the malformed package is the only thing standing in the way, where
under 1.0.0 the chart was out of scope anyway. Accepting the file would still
mean trusting corrupt counts across the other 951 packages, so it remains
rejected.

`o2ma283.ojm` needs the single-page Ogg checksum repair described below, and is
the only package in the corpus that does.

Two tutorial charts render as one second of silence. This is correct rather
than a decoding failure: both declare a hard note count and hard duration of
zero, so no hard chart exists to render.

## Exact release-pair corrections

The compatibility-profile table is intentionally narrow and data-driven. One
profile recognizes only the OMC `o2ma121` pair with OJN SHA-256
`fc0b3d841a0f8fef5fd6a59dcafdf1edff2915016a2715974756fc5fff66df39` and
OJM SHA-256 `d60f4b4beac2bf8638ceaa589ca4d08880718ee7a7e166417b3861d3fa31da30`.
For each easy, normal, and hard chart it validates 1,249 timeline events and the
unique type-4 background event at measure 0, reference ID 1, slot 15/16, then
delays only that event by 2,293 frames. The corresponding M30 release does not
match this pair and remains unchanged. Hashes, cardinality, and the full source
tuple prevent title, filename, package-kind, or difficulty from selecting a
correction accidentally.

Ogg/Vorbis payloads are fully framed and CRC-checked before decoding. A
structurally valid complete stream with a bad non-header data-page checksum is
copied and rewritten at that checksum only, with one warning per sample. The
decoder rejects structural errors, an invalid Vorbis header, and any checksum
problem on BOS/header pages; it still requires a complete libsndfile decode.
This bounded policy follows [RFC 3533](https://www.rfc-editor.org/rfc/rfc3533.html)
and the [Xiph Ogg framing specification](https://xiph.org/ogg/doc/framing.html).

Synthetic fixtures cover parser limits, CLI errors, SHA-256 vectors, exact event
tuples, hash-pair profile guards, Ogg page framing/CRC repair policy, M30 id
mapping, quick-mode exact intra-block scheduling, realtime 48-frame
quantization, and transactional failure. Real OJN/OJM
parity, correlation, and codec measurements must be collected from opt-in
private fixtures before a production compatibility claim is made.

## Verification status

Everything above was verified on Windows x64 and Linux x64, the latter
including the manylinux2014/glibc-2.17 release container with a clean
ASan/UBSan run. Realtime playback through a system audio device was checked by
hand on both a fixed-tempo chart and the corpus chart with the most tempo
changes.

macOS universal and the Windows x86 legacy comparison build are **untested**:
presets exist for both, but no build or test run has been performed on either
because no such machine was available. Treat them as unverified until someone
runs them, and do not infer a compatibility claim from the presence of a
preset.
