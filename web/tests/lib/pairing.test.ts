import { expect, test } from "vitest"

import {
  downloadName,
  extensionOf,
  formatBytes,
  formatDuration,
  isChartFile,
  isPackageFile,
  isPackageWanted,
  isReady,
  normalizePackageName,
  reconcile,
} from "@/lib/pairing"

import { makeChart, makeOjnInfo, makePackage } from "../factories"

/* ------------------------------------------------------------------ *
 * reconcile: the identity contract
 *
 * `paired` feeds both a useMemo and a useEffect in use-queue.ts. If
 * reconcile returns a fresh array when nothing changed, those recompute on
 * every render, and the effect that syncs pairedRef can loop. Reference
 * equality is load-bearing, so these assert `toBe`, never `toEqual`.
 * ------------------------------------------------------------------ */

test("reconcile returns the same array reference when there are no packages", () => {
  const charts = [makeChart()]
  expect(reconcile(charts, [])).toBe(charts)
})

test("reconcile returns the same array reference when no chart newly pairs", () => {
  const charts = [makeChart({ info: makeOjnInfo({ packageName: "other.ojm" }) })]
  const packages = [makePackage({ fileName: "unrelated.ojm" })]
  expect(reconcile(charts, packages)).toBe(charts)
})

test("reconcile returns the same array reference when a chart is already paired", () => {
  const pkg = makePackage()
  const charts = [makeChart({ pkg })]
  // Re-running with the same package must not produce a new array, or every
  // render would allocate and invalidate downstream memos.
  expect(reconcile(charts, [pkg])).toBe(charts)
})

test("reconcile returns a new array when a chart newly pairs", () => {
  const charts = [makeChart()]
  const result = reconcile(charts, [makePackage()])
  expect(result).not.toBe(charts)
  expect(result[0].pkg).toBeDefined()
})

/* ------------------------------------------------------------------ *
 * reconcile: matching rules
 * ------------------------------------------------------------------ */

test("a chart pairs with the package its header names", () => {
  const chart = makeChart({ info: makeOjnInfo({ packageName: "OZUKI.ojm" }) })
  const pkg = makePackage({ fileName: "OZUKI.ojm" })
  expect(reconcile([chart], [pkg])[0].pkg).toBe(pkg)
})

test("pairing does not depend on which file was dropped first", () => {
  // The UX claim is that a package dropped now pairs with a chart parsed a
  // moment later, and the reverse, with no ordering rule between them.
  const chart = makeChart()
  const pkg = makePackage()

  const packageFirst = reconcile([chart], [pkg])[0].pkg
  const chartFirst = reconcile([{ ...chart, pkg: undefined }], [pkg])[0].pkg

  expect(packageFirst).toBe(pkg)
  expect(chartFirst).toBe(pkg)
})

test("one package satisfies every chart that names it", () => {
  // The three difficulties of one song share a single package, so a match is
  // never consumed exclusively by the first chart to claim it.
  const info = makeOjnInfo({ packageName: "shared.ojm" })
  const charts = [makeChart({ info }), makeChart({ info }), makeChart({ info })]
  const pkg = makePackage({ fileName: "shared.ojm" })

  const result = reconcile(charts, [pkg])
  expect(result.map((chart) => chart.pkg)).toEqual([pkg, pkg, pkg])
})

test("the first package of a duplicate name wins over later drops", () => {
  // Re-dropping a folder must not re-point existing pairings at new objects,
  // which would invalidate every row for no reason.
  const first = makePackage({ fileName: "dup.ojm" })
  const second = makePackage({ fileName: "dup.ojm" })
  const chart = makeChart({ info: makeOjnInfo({ packageName: "dup.ojm" }) })

  expect(reconcile([chart], [first, second])[0].pkg).toBe(first)
})

test("package matching ignores case and surrounding whitespace on both sides", () => {
  const chart = makeChart({ info: makeOjnInfo({ packageName: "  OZUKI.OJM " }) })
  const pkg = makePackage({ fileName: "ozuki.ojm" })
  expect(reconcile([chart], [pkg])[0].pkg).toBe(pkg)
})

test("a chart whose header has not parsed yet is passed through untouched", () => {
  const chart = makeChart({ info: undefined })
  const result = reconcile([chart], [makePackage()])
  expect(result[0].pkg).toBeUndefined()
})

/* ------------------------------------------------------------------ *
 * isReady / isPackageWanted
 * ------------------------------------------------------------------ */

test("a chart is ready only once it has both a parsed header and a package", () => {
  expect(isReady(makeChart({ pkg: makePackage() }))).toBe(true)
  expect(isReady(makeChart())).toBe(false)
  expect(isReady(makeChart({ info: undefined, pkg: makePackage() }))).toBe(false)
})

test("a package is wanted only by a chart whose header has parsed", () => {
  const pkg = makePackage({ fileName: "wanted.ojm" })
  const parsed = makeChart({ info: makeOjnInfo({ packageName: "wanted.ojm" }) })
  const unparsed = makeChart({ info: undefined })

  expect(isPackageWanted(pkg, [parsed])).toBe(true)
  expect(isPackageWanted(pkg, [unparsed])).toBe(false)
  expect(isPackageWanted(pkg, [])).toBe(false)
})

/* ------------------------------------------------------------------ *
 * File classification
 * ------------------------------------------------------------------ */

test("extensionOf takes the text after the last dot, lowercased", () => {
  expect(extensionOf("song.ojn")).toBe("ojn")
  expect(extensionOf("a.b.OJN")).toBe("ojn")
  expect(extensionOf(".ojn")).toBe("ojn")
  expect(extensionOf("noext")).toBe("")
  expect(extensionOf("trailing.")).toBe("")
})

test("sample packages are recognized by the .ojm extension only", () => {
  // OJM, OMC, and M30 are container *signatures* (the magic bytes at offset 0),
  // not extensions. Every package on disk is named .ojm whatever its internal
  // format, so filtering on .omc/.m30 matched nothing and promised file types
  // that do not exist.
  expect(isPackageFile("o2ma100.ojm")).toBe(true)
  expect(isPackageFile("o2ma100.OJM")).toBe(true)
  expect(isPackageFile("o2ma100.omc")).toBe(false)
  expect(isPackageFile("o2ma100.m30")).toBe(false)
})

test("charts are recognized by the .ojn extension", () => {
  expect(isChartFile("o2ma100.ojn")).toBe(true)
  expect(isChartFile("o2ma100.OJN")).toBe(true)
  expect(isChartFile("o2ma100.ojm")).toBe(false)
})

test("normalizePackageName is the single normalization both sides go through", () => {
  expect(normalizePackageName("  OZUKI.OJM  ")).toBe("ozuki.ojm")
})

/* ------------------------------------------------------------------ *
 * Formatters
 * ------------------------------------------------------------------ */

test("formatDuration rounds to whole seconds before splitting minutes", () => {
  expect(formatDuration(0)).toBe("0:00")
  expect(formatDuration(59.6)).toBe("1:00")
  expect(formatDuration(605)).toBe("10:05")
  expect(formatDuration(3600)).toBe("60:00")
})

test("formatBytes switches from bytes to KB at exactly 1024", () => {
  expect(formatBytes(0)).toBe("0 B")
  expect(formatBytes(1023)).toBe("1023 B")
  expect(formatBytes(1024)).toBe("1.0 KB")
})

test("formatBytes drops the decimal at and above ten units", () => {
  expect(formatBytes(10239)).toBe("10.0 KB")
  expect(formatBytes(10240)).toBe("10 KB")
})

test("formatBytes promotes to the next unit just below a boundary", () => {
  // Regression: the loop tests the *divided* value, so 1048575/1024 = 1023.999
  // failed the >= 1024 check and stayed in KB -- then Math.round rendered it as
  // "1024 KB". Any render just under a MiB hit this, which is common.
  expect(formatBytes(1048575)).toBe("1.0 MB")
  expect(formatBytes(1073741823)).toBe("1.0 GB")
})

test("formatBytes saturates at gigabytes rather than inventing a larger unit", () => {
  expect(formatBytes(5 * 1024 ** 3)).toBe("5.0 GB")
  expect(formatBytes(2048 * 1024 ** 3)).toMatch(/GB$/)
})

/* ------------------------------------------------------------------ *
 * downloadName
 * ------------------------------------------------------------------ */

test("downloadName combines the title, difficulty, and format", () => {
  const chart = makeChart({ info: makeOjnInfo({ title: "Ruthless" }) })
  expect(downloadName(chart, "Hard", "ogg")).toBe("Ruthless [Hard].ogg")
})

test("downloadName replaces characters a filesystem would reject", () => {
  const chart = makeChart({ info: makeOjnInfo({ title: 'a/b\\c:d*e?f"g<h>i|j' }) })
  expect(downloadName(chart, "Hard", "wav")).toBe("a_b_c_d_e_f_g_h_i_j [Hard].wav")
})

test("downloadName sanitizes an all-illegal title rather than falling back", () => {
  // Each illegal character maps to "_", so "///" becomes "___" and is truthy --
  // the filename fallback never fires here. Pinned because the behaviour reads
  // the other way at a glance.
  const chart = makeChart({ info: makeOjnInfo({ title: "///" }) })
  expect(downloadName(chart, "Hard", "mp3")).toBe("___ [Hard].mp3")
})

test("downloadName falls back to the chart filename when the title is blank", () => {
  const blank = makeChart({
    fileName: "o2ma704.ojn",
    info: makeOjnInfo({ title: "   " }),
  })
  expect(downloadName(blank, "Hard", "ogg")).toBe("o2ma704 [Hard].ogg")

  const unparsed = makeChart({ fileName: "o2ma704.OJN", info: undefined })
  expect(downloadName(unparsed, "Normal", "wav")).toBe("o2ma704 [Normal].wav")
})
