import type { ChartEntry, SamplePackage } from "@/lib/pairing"
import type { DifficultyInfo, OjnInfo } from "@/lib/renderojn"

/*
 * Builders for the shapes the queue works with. They exist so a test can state
 * only the field it cares about -- a case about pairing should not have to
 * spell out a tempo.
 */

let counter = 0
const nextId = () => `t${++counter}`

export function makeOjnInfo(overrides: Partial<OjnInfo> = {}): OjnInfo {
  const difficulties: DifficultyInfo[] = overrides.difficulties ?? [
    { difficulty: 0, noteCount: 300, durationSeconds: 100 },
    { difficulty: 1, noteCount: 500, durationSeconds: 100 },
    { difficulty: 2, noteCount: 700, durationSeconds: 100 },
  ]
  return {
    title: "Bach Alive",
    artist: "Beautiful Day",
    charter: "Hiro",
    packageName: "o2ma100.ojm",
    genre: "Dance",
    songId: 100,
    tempo: 130,
    ...overrides,
    difficulties,
  }
}

export function makeChart(overrides: Partial<ChartEntry> = {}): ChartEntry {
  return {
    id: nextId(),
    fileName: "o2ma100.ojn",
    file: new File([new Uint8Array([1, 2, 3])], "o2ma100.ojn"),
    info: makeOjnInfo(),
    render: { status: "idle" },
    ...overrides,
  }
}

export function makePackage(
  overrides: Partial<SamplePackage> = {}
): SamplePackage {
  return {
    id: nextId(),
    fileName: "o2ma100.ojm",
    file: new File([new Uint8Array([4, 5, 6])], "o2ma100.ojm"),
    ...overrides,
  }
}
