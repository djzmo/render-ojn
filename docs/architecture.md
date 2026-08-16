# RenderOJN architecture

`RenderOJN` is the only public API.  The executable orchestrates the internal
static `renderojn_core` layers:

1. `core/io` reads immutable, size-capped byte buffers and provides checked
   little-endian reads plus transactional output paths.
2. `core/crypto` supplies an offline SHA-256 primitive used to bind compatibility
   profiles to exact immutable input pairs.
3. `core/format` normalizes ordinary OJN input, parses bounded timelines,
   extracts the header's cover art, and parses M30/OMC/OJM package records
   with explicit kind and flag validation.  `core/text` decodes the header's
   CP949 text to UTF-8 from a generated table (tools/cp949), so the same code
   serves the WebAssembly build.
4. `core/audio` validates Ogg/Vorbis framing before in-memory libsndfile decode,
   then resamples linearly to float32 stereo/48 kHz; miniaudio provides realtime playback.
5. `core/render` applies note mapping and mixes a maximum of 100 voices
   deterministically. File output uses quick scheduling -- unpaced 1,024-frame
   blocks placing triggers at exact intra-block frame offsets; the realtime
   48-frame scheduling mode remains for live playback (`--play`).
6. `core/output` writes PCM16 WAV, LAME MP3, or Vorbis Ogg transactionally and
   applies metadata, including the cover art as an ID3v2 APIC frame or a Xiph
   METADATA_BLOCK_PICTURE.

OJN chart parsing is deliberately a two-pass timeline operation. It first
validates the declared package/count boundaries and collects raw channel-0
measure fractions, channel-1 BPM changes, and note records with their exact
`measure + slot_index / slot_count` positions. It then applies sorted prefix
measure adjustments, orders tempo changes before notes at equal positions,
integrates BPM chronologically, and rounds the resulting seconds once to
48 kHz frames. The mixer therefore receives an already-normalized timeline;
file output uses those frames for exact intra-block placement, while live
playback retains the 48-frame scheduling behavior.

The OJN normalizer is intentionally a separate seam, and it is where Korea-era
`new` wrappers are handled.  A `new` file holds an ordinary OJN behind a byte
reversal and a repeating XOR key whose parameters are stored in the wrapper's
own 8-byte header: block size, then the main, mid, and initial key bytes.  The
normalizer rebuilds that key, decrypts the payload backwards from the end of the
file, and requires the result to carry the `ojn\0` signature before returning
it.  Everything downstream therefore sees an ordinary buffer and needs no
knowledge of the container.

Requiring the signature matters: a wrong key produces plausible-looking bytes
rather than an obvious failure, so without that check a mis-keyed file would be
parsed as though it had decrypted correctly.

Compatibility profiles run only after both immutable inputs and the parsed
timeline are available. A profile is keyed by both complete SHA-256 digests,
checks the selected chart's cardinality and exact source event tuple, performs a
checked frame adjustment, then stably restores timeline order. A profile whose
release data no longer matches fails explicitly rather than modifying a nearby
event.

The Ogg/Vorbis seam consumes an entire encoded buffer as pages before libsndfile
sees it. It checks capture/version, checked lace and body lengths, one logical
stream serial, page sequence and continuation/BOS/EOS flag consistency, and the
three Vorbis header packets. It computes each page's Ogg CRC with the checksum
field zeroed. Only a mismatching late data page in an otherwise valid stream is
copied and repaired; a BOS or header-page mismatch is rejected. This follows the
Ogg page framing and checksum rules in [RFC 3533](https://www.rfc-editor.org/rfc/rfc3533.html)
and the [Xiph Ogg framing specification](https://xiph.org/ogg/doc/framing.html).
