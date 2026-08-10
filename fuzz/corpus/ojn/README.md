Fuzz seed corpus. Add only redistributable malformed or synthetic OJN inputs here.

- `exact-five-way.seed`: non-divisible five-way note subdivision.
- `measure-fraction.seed`: channel-0 fraction normalization.
- `bpm-channel-order.seed`: channel-order backtracking around a BPM change.
- `count-mismatch.seed`: declared event count differs from the chart section.
- `truncated-header.seed`: header shorter than the fixed 300 bytes.

Korea-era `new` wrapper seeds. These matter because decryption accepts input the
parser previously rejected at the signature check, so the reachable surface is
wider than it was before 1.0.1:

- `new-wrapper.seed`: a valid wrapper, block size 11, that decrypts to an
  ordinary OJN.
- `new-wrapper-block4.seed`: the same with block size 4 and different keys, so
  the key layout is exercised at both ends of the observed range.
- `new-wrapper-zero-block.seed`: zero block size, which would divide by zero and
  index an empty key buffer.
- `new-wrapper-truncated.seed`: shorter than the 8-byte key header.
- `new-wrapper-wrong-key.seed`: a well-formed container whose payload does not
  decrypt to an OJN, covering the signature check that stops garbage from
  reaching the parser.
