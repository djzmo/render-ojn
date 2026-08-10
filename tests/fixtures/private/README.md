# Private fixture opt-in

This ignored directory is intentionally empty in source control. You may place
representative OJN/OJM assets here for local comparison. Add a JSON manifest
with these fields for each case:

```json
{
  "inputs": { "ojn_sha256": "...", "ojm_sha256": "..." },
  "metadata": { "title": "...", "artist": "...", "song_id": 0, "genre": "..." },
  "events": [["frame", "ref_id", "note_type"]],
  "package": { "kind": "M30", "sample_ids": [1] },
  "duration_frames": 0,
  "tags": { "title": "...", "artist": "...", "track": 0, "genre": "...", "comment": "..." },
  "audio": { "pcm_sha256": "...", "left_rms": 0, "right_rms": 0, "peak": 0 },
  "compatibility": { "profile_warning": "...", "trigger_delta_frames": 0 },
  "ogg": { "pages": 0, "crc_repairs": 0, "warning": "..." }
}
```

Run `tools/CaptureLegacy.ps1` only with explicit paths to a user-owned legacy
runtime; it copies those inputs into a temporary directory before execution.

## Known-good render baseline

One private pair has a recorded output hash, useful as a regression guard when
changing the parser, scheduler, or mixer. Rendering `o2ma100` at hard
difficulty in quick mode to WAV produces 4,992,000 frames of PCM16 stereo at
48 kHz whose SHA-256 is
`061e9e6ed9c849813ade26a0b8e31dc686a48bb32656d8e39a0cefa6437956f5`, together
with one warning about a zero-byte Ogg directory record. The render is
audition-confirmed, so a change to this hash means audible output changed and
should be explained rather than accepted.

An older value, `979f526f31f3ca51cd27c2506667adabe98e671ba3c6975b8d81811650a7dc1e`,
predates exact intra-block onset placement and is superseded; a mismatch
against it is not a regression. Note that this baseline covers only charts that
do not overrun their declared duration — for one that does, output length
follows content, so the hash necessarily differs from any duration-bounded
render.

For a private Ogg audit, record package/sample/page totals, structural failures,
and checksum repairs separately from the source assets. One opt-in audit across
three installed Music roots observed 980 packages (15 OMC, 13 OJM, 952 M30), 87
embedded Ogg samples, and 11,832 pages: zero package parse or structural Ogg
failures, and one repaired data-page checksum in the NOWCOM `o2ma283` background
sample. This is characterization evidence, not a runtime path or an asset
requirement; never commit the files or their absolute locations.
