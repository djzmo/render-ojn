/**
 * Stand-in for the WASM module while the C++ track builds it.
 *
 * Everything here is fake, but deterministically so: the same bytes always
 * produce the same header, so pairing, difficulty selection and re-renders
 * behave consistently across a session. Delete this file once
 * `loadRenderOjn` points at the real binding.
 */
import { PACKAGE_EXTENSIONS } from "./pairing"
import {
  OjnParseError,
  type Difficulty,
  type DifficultyInfo,
  type OjnInfo,
  type OutputFormat,
  type ProgressCallback,
  type Quality,
  type RenderMode,
  type RenderOjnModule,
  type RenderResult,
  type Tracks,
} from "./renderojn"

const TITLES = [
  "Ruthless",
  "Xtreme",
  "Rise of Mine",
  "Beyond the Beat",
  "Lucid Dream",
  "Escape from the City",
  "Nightmare",
  "Crescent Moon",
]

const ARTISTS = [
  "SoundTeMP",
  "Ruby Tuesday",
  "M2U",
  "Nauts",
  "Cranky",
  "Tsukasa",
]

const PACKAGE_STEMS = [
  "OZUKI",
  "SUNNY",
  "AURORA",
  "KAIZEN",
  "MIRAGE",
  "ZENITH",
]

/* Real packages are always `.ojm`; OMC and M30 are signatures, not names. */
const PACKAGE_EXTENSION = PACKAGE_EXTENSIONS[0]

function hash(bytes: Uint8Array): number {
  // FNV-1a over a bounded prefix — enough to be stable, cheap on big files.
  let h = 0x811c9dc5
  const limit = Math.min(bytes.length, 4096)
  for (let i = 0; i < limit; i++) {
    h ^= bytes[i]
    h = Math.imul(h, 0x01000193) >>> 0
  }
  // Length participates so two files sharing a prefix still differ.
  h ^= bytes.length
  return h >>> 0
}

/**
 * Picks by a mixed slice of the seed. `salt` decorrelates the fields, so two
 * files do not land on the same title just because their hashes share bits.
 */
function pick<T>(items: readonly T[], seed: number, salt: number): T {
  let h = (seed ^ Math.imul(salt + 1, 0x9e3779b1)) >>> 0
  h = (h ^ (h >>> 15)) >>> 0
  h = Math.imul(h, 0x85ebca6b) >>> 0
  // Each step re-coerces to unsigned: XOR alone yields a signed int32, and a
  // negative operand would make the modulo below index off the end.
  h = (h ^ (h >>> 13)) >>> 0
  return items[h % items.length]
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function buildDifficulties(seed: number): DifficultyInfo[] {
  // Most charts carry all three; some carry fewer, which the UI must handle.
  const available: Difficulty[] =
    seed % 7 === 0 ? [2] : seed % 5 === 0 ? [1, 2] : [0, 1, 2]

  const baseDuration = 95 + (seed % 140)

  return available.map((difficulty) => ({
    difficulty,
    noteCount: 320 + difficulty * 480 + ((seed >> (difficulty + 1)) % 700),
    durationSeconds: baseDuration + difficulty * 2,
  }))
}

async function readOjnInfo(bytes: Uint8Array): Promise<OjnInfo> {
  await delay(90 + (bytes.length % 160))

  if (bytes.length < 300) {
    throw new OjnParseError(
      "File is too small to contain an OJN header. It may be truncated."
    )
  }

  const seed = hash(bytes)
  const stem = pick(PACKAGE_STEMS, seed, 3)

  return {
    title: pick(TITLES, seed, 1),
    artist: pick(ARTISTS, seed, 2),
    charter: pick(ARTISTS, seed, 5),
    packageName: `${stem}.${PACKAGE_EXTENSION}`,
    genre: "Dance",
    songId: seed % 1000,
    tempo: 120 + (seed % 80),
    difficulties: buildDifficulties(seed),
  }
}

async function render(
  ojn: Uint8Array,
  ojm: Uint8Array,
  difficulty: Difficulty,
  format: OutputFormat,
  quality: Quality,
  // Accepted to match the real module's signature; the fake output does not
  // vary by role or scheduling, but the parameters must exist so callers
  // type-check.
  _tracks: Tracks,
  _renderMode: RenderMode,
  onProgress: ProgressCallback
): Promise<RenderResult> {
  const steps = 40
  // Longer for higher quality and bigger packages, so progress feels earned.
  const perStep = 24 + quality * 10 + Math.min(ojm.length / 240_000, 26)

  for (let step = 1; step <= steps; step++) {
    await delay(perStep)
    onProgress(step / steps)
  }

  // A byte count in the right ballpark, so the size column reads plausibly.
  const seconds = 100 + difficulty * 12 + (hash(ojn) % 90)
  const bytesPerSecond =
    format === "wav" ? 176_400 : format === "mp3" ? 24_000 : 20_000
  const size = Math.round(seconds * bytesPerSecond)

  const out = new Uint8Array(size)
  // A recognizable header keeps a downloaded file from looking like garbage.
  const signature =
    format === "wav" ? "RIFF" : format === "ogg" ? "OggS" : "ID3"
  for (let i = 0; i < signature.length; i++) {
    out[i] = signature.charCodeAt(i)
  }
  return { bytes: out, warnings: [] }
}

export function createStubModule(): RenderOjnModule {
  return { readOjnInfo, render }
}
