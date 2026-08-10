Fuzz seed corpus. Add only redistributable malformed or synthetic sample packages here.

Named structural seeds cover the 1.0.0 M30 baseline and its rejection paths:

| Seed | Shape | Expected parser behavior |
|---|---|---|
| `m30-flag-0.seed` | plaintext Ogg payload | accepted, bytes preserved |
| `m30-flag-16.seed` | `nami` obfuscated payload | accepted after XOR |
| `m30-flag-32.seed` | plaintext Ogg payload | accepted, bytes preserved |
| `m30-nami-partial-tail.seed` | payload with a trailing partial group | accepted; the 0-3 byte tail is untouched |
| `m30-codec-0.seed` | codec code 0 | accepted into the background bank |
| `m30-flag-unsupported.seed` | flag 8 | rejected as an unsupported encoding |
| `m30-codec-invalid.seed` | codec code 1 | rejected as an unsupported codec |
| `m30-not-ogg.seed` | decoded payload without `OggS` | rejected as malformed |

Directory-shape seeds:

| Seed | Shape | Expected parser behavior |
|---|---|---|
| `sparse-pcm-slot.ojm` | zero-length PCM slot | slot skipped, positional IDs retained |
| `trailing-empty-ogg.ojm` | trailing zero-byte Ogg records | accepted and counted |
| `unsupported-m30-codec.m30` | unsupported codec code | rejected as unsupported |

The remaining hash-named entries are minimized fuzzer discoveries. Keep only
deterministic, minimized inputs.
