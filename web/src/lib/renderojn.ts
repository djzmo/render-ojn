/**
 * Typed wrapper over the RenderOJN WebAssembly module.
 *
 * This is the only file that knows which implementation is live. The real
 * renderer runs in a Web Worker; `stub.ts` stands in behind
 * VITE_RENDEROJN_STUB for UI work without an Emscripten toolchain. Nothing
 * outside this file should need to change to switch between them.
 */

/** Difficulty slot inside an OJN. The file carries up to three. */
export type Difficulty = 0 | 1 | 2

export const DIFFICULTY_NAMES: Record<Difficulty, string> = {
  0: "Easy",
  1: "Normal",
  2: "Hard",
}

/** Matches the CLI default (`src/app/Cli.hpp`). */
export const DEFAULT_DIFFICULTY: Difficulty = 2

export interface DifficultyInfo {
  difficulty: Difficulty
  noteCount: number
  durationSeconds: number
}

export interface OjnInfo {
  title: string
  artist: string
  charter: string
  /** The sample package this chart needs, exactly as the header names it. */
  packageName: string
  genre: string
  songId: number
  tempo: number
  difficulties: DifficultyInfo[]
}

export type OutputFormat = "wav" | "ogg" | "mp3"

/**
 * Display order for the format control — chosen for the UI, not for the module.
 * Never derive the module's format argument from this array's index: the C++
 * enum is `{Wav = 0, Mp3 = 1, Ogg = 2}`, so an index lookup would hand OGG the
 * value that means MP3. Use `FORMAT_VALUES` for that.
 */
export const OUTPUT_FORMATS: readonly OutputFormat[] = ["wav", "ogg", "mp3"]

/** Mirrors `output::Format` in `src/core/output/Encoder.hpp`. */
export const FORMAT_VALUES: Record<OutputFormat, 0 | 1 | 2> = {
  wav: 0,
  mp3: 1,
  ogg: 2,
}

/** Matches the CLI's `--quality` scale, which accepts 1, 2, or 3. */
export type Quality = 1 | 2 | 3

/**
 * Labelled by what the setting actually produces, not by a verdict on it.
 * "Draft/Standard/High" described effort; the thing that changes is the MP3
 * bitrate (and the equivalent Vorbis VBR level), so the control says that.
 * Values come from `mp3_quality_for` in `src/core/output/Encoder.cpp`.
 */
export const QUALITY_LABELS: Record<Quality, string> = {
  1: "128k",
  2: "192k",
  3: "320k",
}

/** Long form for the control's description and tooltips. */
export const QUALITY_DETAIL: Record<Quality, string> = {
  1: "128 kbps MP3 · Vorbis quality 0.5 · smallest file",
  2: "192 kbps MP3 · Vorbis quality 0.8",
  3: "320 kbps MP3 · Vorbis quality 1.0 · best quality",
}

export const QUALITY_VALUES: readonly Quality[] = [1, 2, 3]

/** The CLI's default (`src/app/Cli.hpp`). */
export const DEFAULT_QUALITY: Quality = 3

/**
 * Which note roles to sound. All three renders are the same length: the
 * unselected notes are muted, not removed, so timing is preserved (see
 * `render::TrackSelection` in `src/core/render/Mixer.hpp`). Keysounds are the
 * playable lanes; Background is the autoplay/BGM stream. A chart may have an
 * empty role, which renders silence — the core warns when that happens.
 */
export type Tracks = "all" | "keysounds" | "background"

export const TRACKS_OPTIONS: readonly Tracks[] = ["all", "keysounds", "background"]

/** Mirrors `render::TrackSelection` in `src/core/render/Mixer.hpp`. */
export const TRACKS_VALUES: Record<Tracks, 0 | 1 | 2> = {
  all: 0,
  keysounds: 1,
  background: 2,
}

/** Short control labels. */
export const TRACKS_LABELS: Record<Tracks, string> = {
  all: "All",
  keysounds: "Keys",
  background: "BGM",
}

/** Long form for the control's tooltips and description. */
export const TRACKS_DETAIL: Record<Tracks, string> = {
  all: "Every note — the full mix",
  keysounds: "Playable lanes only — the notes you hit",
  background: "Autoplay/BGM only — varies by chart, may have gaps",
}

/** Matches the CLI default (`--tracks all`). */
export const DEFAULT_TRACKS: Tracks = "all"

/** Fraction in [0, 1]. Posted from the mixer's PcmConsumer callback. */
export type ProgressCallback = (fraction: number) => void

/** A warning the core raised while parsing or rendering. Shown on the row. */
export type Warning = string

export interface RenderResult {
  bytes: Uint8Array
  warnings: Warning[]
}

export interface RenderOjnModule {
  /**
   * Parses an OJN header. Cheap — no sample package required, which is what
   * lets a dropped file become a row immediately.
   */
  readOjnInfo(bytes: Uint8Array): Promise<OjnInfo>

  /** Full render. Requires both the chart and its sample package. */
  render(
    ojn: Uint8Array,
    ojm: Uint8Array,
    difficulty: Difficulty,
    format: OutputFormat,
    quality: Quality,
    tracks: Tracks,
    onProgress: ProgressCallback
  ): Promise<RenderResult>
}

/** Thrown for a file the parser cannot read. Surfaced on the row itself. */
export class OjnParseError extends Error {
  constructor(message: string) {
    super(message)
    this.name = "OjnParseError"
  }
}

export const MIME_TYPES: Record<OutputFormat, string> = {
  wav: "audio/wav",
  ogg: "audio/ogg",
  mp3: "audio/mpeg",
}

let modulePromise: Promise<RenderOjnModule> | null = null

/**
 * Resolves the module, loading it once and reusing it thereafter.
 *
 * The real renderer lives in a Web Worker so a multi-minute mix never blocks
 * the page. Set `VITE_RENDEROJN_STUB=1` to fall back to the fake module — the
 * UI can then be developed without an Emscripten toolchain.
 */
export function loadRenderOjn(): Promise<RenderOjnModule> {
  if (!modulePromise) {
    modulePromise = import.meta.env.VITE_RENDEROJN_STUB
      ? import("./stub").then((m) => m.createStubModule())
      : import("./worker-client").then((m) => m.createWorkerModule())
  }
  return modulePromise
}
