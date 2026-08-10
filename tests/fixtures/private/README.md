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

For a private Ogg audit, record package/sample/page totals, structural failures,
and checksum repairs separately from the source assets. One opt-in audit across
three installed Music roots observed 980 packages (15 OMC, 13 OJM, 952 M30), 87
embedded Ogg samples, and 11,832 pages: zero package parse or structural Ogg
failures, and one repaired data-page checksum in the NOWCOM `o2ma283` background
sample. This is characterization evidence, not a runtime path or an asset
requirement; never commit the files or their absolute locations.
