/**
 * OJN ↔ sample-package matching.
 *
 * An OJN header names the package it needs, so pairing needs no guesswork:
 * every dropped package is matched against that name, case-insensitively.
 * Packages that match nothing are held — a later drop may claim them.
 */
import { TRACK_SUFFIX } from "./renderojn"
import type { Difficulty, OjnInfo, OutputFormat, Tracks } from "./renderojn"

/**
 * Sample packages are always named `.ojm`.
 *
 * OJM, OMC, and M30 are the three container *signatures* the core recognises
 * (the four magic bytes at offset 0 — see `parse_sample_package`), not
 * extensions. Every file in the real corpus ends in `.ojm` regardless of which
 * of the three it actually is, so filtering on `.omc` or `.m30` would match
 * nothing while implying files that do not exist.
 */
export const PACKAGE_EXTENSIONS = ["ojm"] as const
export const CHART_EXTENSION = "ojn"

export function extensionOf(fileName: string): string {
  const dot = fileName.lastIndexOf(".")
  return dot === -1 ? "" : fileName.slice(dot + 1).toLowerCase()
}

export function isChartFile(fileName: string): boolean {
  return extensionOf(fileName) === CHART_EXTENSION
}

export function isPackageFile(fileName: string): boolean {
  return (PACKAGE_EXTENSIONS as readonly string[]).includes(
    extensionOf(fileName)
  )
}

/** The single normalization both sides of a match go through. */
export function normalizePackageName(name: string): string {
  return name.trim().toLowerCase()
}

/*
 * Files are held as `File` handles, never as decoded bytes.
 *
 * A `File` is a reference to data the browser keeps on disk; reading it
 * produces an ArrayBuffer that lives in the JS heap. Sample packages run
 * 3.7-30 MB each, so retaining decoded bytes for a whole folder meant hundreds
 * of megabytes resident for as long as the queue existed -- which is what ran
 * the tab out of memory on a large import. Reading at the point of use keeps
 * the resident set proportional to what is being worked on, not to how much
 * was dropped.
 */
export interface SamplePackage {
  id: string
  fileName: string
  file: File
}

export type RenderState =
  | { status: "idle" }
  | { status: "rendering"; progress: number }
  | {
      status: "done"
      url: string
      size: number
      format: OutputFormat
      /** The selection this file was rendered with; named into the download. */
      tracks: Tracks
    }
  | { status: "failed"; message: string }

export interface ChartEntry {
  id: string
  fileName: string
  /** Read on demand; see the note on SamplePackage. */
  file: File
  /** Absent until the header parse resolves. */
  info?: OjnInfo
  /** Set when the header cannot be read; the row shows this verbatim. */
  parseError?: string
  /** The package this chart needs, once one has arrived. */
  pkg?: SamplePackage
  difficulty?: Difficulty
  /** Diagnostics from the last render — shown on the row, as the CLI prints them. */
  warnings?: string[]
  render: RenderState
}

export function isReady(
  entry: ChartEntry
): entry is ChartEntry & { info: OjnInfo; pkg: SamplePackage } {
  return Boolean(entry.info && entry.pkg)
}

/**
 * Attaches each chart to the package its header names.
 *
 * This is a pure derivation over both lists rather than stored state: a
 * package dropped now must pair with a chart whose header parses a moment
 * later, and vice versa, without any ordering rule between the two.
 *
 * A single package can satisfy several charts — the difficulties of one song
 * share a package — so a match is never consumed exclusively.
 */
export function reconcile(
  charts: ChartEntry[],
  packages: SamplePackage[]
): ChartEntry[] {
  if (!packages.length) return charts

  const byName = new Map<string, SamplePackage>()
  for (const pkg of packages) {
    // First drop of a given name wins; a re-drop must not disturb pairings.
    const key = normalizePackageName(pkg.fileName)
    if (!byName.has(key)) {
      byName.set(key, pkg)
    }
  }

  let changed = false
  const next = charts.map((chart) => {
    if (!chart.info) return chart
    const match = byName.get(normalizePackageName(chart.info.packageName))
    if (!match || chart.pkg?.id === match.id) return chart
    changed = true
    return { ...chart, pkg: match }
  })

  return changed ? next : charts
}

/** True while some chart names this package — i.e. it is not merely held. */
export function isPackageWanted(
  pkg: SamplePackage,
  charts: ChartEntry[]
): boolean {
  const key = normalizePackageName(pkg.fileName)
  return charts.some(
    (chart) =>
      chart.info && normalizePackageName(chart.info.packageName) === key
  )
}

export function formatDuration(seconds: number): string {
  const total = Math.round(seconds)
  const minutes = Math.floor(total / 60)
  return `${minutes}:${String(total % 60).padStart(2, "0")}`
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  const units = ["KB", "MB", "GB"]
  let value = bytes / 1024
  let unit = 0
  // Promote on the *rendered* value, not the raw one: 1048575 divides to
  // 1023.999, which passes a `< 1024` check but then rounds to "1024 KB".
  // Rounding first means the decision matches what the user will see.
  const rendered = (candidate: number) =>
    candidate < 10 ? Number(candidate.toFixed(1)) : Math.round(candidate)
  while (rendered(value) >= 1024 && unit < units.length - 1) {
    value /= 1024
    unit++
  }
  return `${value < 10 ? value.toFixed(1) : Math.round(value)} ${units[unit]}`
}

/**
 * `Ruthless [Hard].ogg`, or `Ruthless [Hard]_background.ogg` for a stem —
 * readable, stable across re-renders, and the track suffix sits against the
 * extension exactly as the CLI's does.
 */
export function downloadName(
  entry: ChartEntry,
  difficultyName: string,
  format: OutputFormat,
  tracks: Tracks = "all"
): string {
  const stem =
    entry.info?.title.replace(/[\\/:*?"<>|]/g, "_").trim() ||
    entry.fileName.replace(/\.ojn$/i, "")
  return `${stem} [${difficultyName}]${TRACK_SUFFIX[tracks]}.${format}`
}
