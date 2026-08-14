import * as React from "react"
import { toast } from "sonner"

import {
  downloadName,
  isChartFile,
  isPackageFile,
  isPackageWanted,
  isReady,
  reconcile,
  type ChartEntry,
  type SamplePackage,
} from "@/lib/pairing"
import { createZip } from "@/lib/zip"
import {
  DEFAULT_DIFFICULTY,
  DEFAULT_QUALITY,
  DEFAULT_TRACKS,
  DIFFICULTY_NAMES,
  loadRenderOjn,
  MIME_TYPES,
  type Difficulty,
  type OutputFormat,
  type Quality,
  type Tracks,
} from "@/lib/renderojn"

let nextId = 0
const makeId = () => `f${++nextId}`

/**
 * The message from anything thrown, including a DOMException.
 *
 * `instanceof Error` is too narrow here: a File read that fails rejects with a
 * DOMException, which is not an Error instance in every environment, and the
 * refactor to read files lazily made that reachable — the window between
 * dropping a file and reading it is now however long the row sits in the
 * queue, so a file moved or deleted in the meantime is an ordinary outcome.
 * Narrowing on the message itself keeps "file moved" instead of replacing it
 * with a generic string that cannot be told apart from a corrupt chart.
 */
function messageFrom(error: unknown, fallback: string): string {
  if (typeof error === "object" && error !== null && "message" in error) {
    const message = (error as { message: unknown }).message
    if (typeof message === "string" && message) return message
  }
  return fallback
}

/**
 * Owns the whole ingest → pair → render lifecycle.
 *
 * Charts and held packages are separate lists because they have different
 * lifetimes: a chart is a row the moment it lands, a package is invisible
 * until something claims it.
 */
export function useQueue() {
  const [charts, setCharts] = React.useState<ChartEntry[]>([])
  const [packages, setPackages] = React.useState<SamplePackage[]>([])
  const [format, setFormatState] = React.useState<OutputFormat>("ogg")
  const [quality, setQualityState] = React.useState<Quality>(DEFAULT_QUALITY)
  const [tracks, setTracksState] = React.useState<Tracks>(DEFAULT_TRACKS)
  const [rejected, setRejected] = React.useState<string[]>([])
  // Building the archive copies every rendered file; on a long queue that is
  // slow enough to need its own state so the button can say so.
  const [isPacking, setIsPacking] = React.useState(false)

  // Object URLs outlive React state, so they are tracked for explicit revoke.
  const objectUrls = React.useRef(new Set<string>())
  React.useEffect(() => {
    const urls = objectUrls.current
    return () => {
      for (const url of urls) URL.revokeObjectURL(url)
    }
  }, [])

  const updateChart = React.useCallback(
    (id: string, patch: (chart: ChartEntry) => ChartEntry) => {
      setCharts((current) =>
        current.map((chart) => (chart.id === id ? patch(chart) : chart))
      )
    },
    []
  )

  const addFiles = React.useCallback(async (files: File[]) => {
    const charts = files.filter((file) => isChartFile(file.name))
    const packages = files.filter((file) => isPackageFile(file.name))
    const unknown = files.filter(
      (file) => !isChartFile(file.name) && !isPackageFile(file.name)
    )

    if (unknown.length) {
      // Accumulate: a second drop must not erase names from the first that the
      // user has not dismissed yet, or those files look accepted. Deduplicated
      // so re-dropping the same folder does not repeat a name.
      setRejected((current) => [
        ...current,
        ...unknown
          .map((file) => file.name)
          .filter((name) => !current.includes(name)),
      ])
    }

    /*
     * Packages land first so a chart parsed a moment later pairs immediately.
     *
     * Only the File handles are stored. Reading them here would put the whole
     * folder's bytes in the heap and keep them there for the life of the
     * queue; a hundred songs is well over a gigabyte, which is what ran the
     * tab out of memory. The handles cost nothing until something needs them.
     */
    if (packages.length) {
      const loaded: SamplePackage[] = packages.map((file) => ({
        id: makeId(),
        fileName: file.name,
        file,
      }))
      setPackages((current) => [...current, ...loaded])
    }

    if (charts.length) {
      const loaded = charts.map((file) => ({
        id: makeId(),
        fileName: file.name,
        file,
        render: { status: "idle" as const },
      }))
      setCharts((current) => [...current, ...loaded])

      const module = await loadRenderOjn()
      /*
       * Parse one chart at a time. The worker handles messages serially, so
       * firing N parses at once does not make them finish sooner -- it just
       * queues N copies of N charts in the worker's inbox at the same time,
       * and leaves the later rows sitting as skeletons until the queue drains.
       * Sequential parsing resolves each row as it completes, which is both
       * cheaper and what the UI already implies is happening.
       */
      for (const entry of loaded) {
        try {
          // Read one chart at a time and let each buffer go before the next.
          // Charts are small, but a folder of them still adds up if every one
          // is held at once.
          const info = await module.readOjnInfo(
            new Uint8Array(await entry.file.arrayBuffer())
          )
          const difficulty = info.difficulties.some(
            (d) => d.difficulty === DEFAULT_DIFFICULTY
          )
            ? DEFAULT_DIFFICULTY
            : (info.difficulties.at(-1)?.difficulty ?? DEFAULT_DIFFICULTY)

          setCharts((current) =>
            current.map((chart) =>
              chart.id === entry.id ? { ...chart, info, difficulty } : chart
            )
          )
        } catch (error) {
          const message = messageFrom(error, "Could not read the file.")
          setCharts((current) =>
            current.map((chart) =>
              chart.id === entry.id ? { ...chart, parseError: message } : chart
            )
          )
        }
      }
    }
  }, [])

  /*
   * Pairing is derived, not stored. A chart is paired iff some dropped package
   * matches the name its header states, so recomputing beats keeping a second
   * copy of that fact in sync — and it means a package dropped now pairs with
   * a chart parsed later without any reconciliation pass.
   */
  const paired = React.useMemo(
    () => reconcile(charts, packages),
    [charts, packages]
  )

  /*
   * The latest derived list, readable from async work that outlives the render
   * it started in. renderAll awaits one row at a time, so anything it reads
   * from a closure describes the queue as it was when the button was clicked.
   */
  const pairedRef = React.useRef(paired)
  // Assigned during render, not in an effect. renderAll reads this immediately
  // after `await renderOne(...)` settles, which happens in the same microtask
  // turn as the state update -- before React commits, so an effect-written ref
  // would still hold the pre-removal queue and the loop would render a row the
  // user had just deleted. Writing during render is safe here because the value
  // is derived from the state being rendered, not from an external source.
  pairedRef.current = paired

  // Packages nothing claims are held quietly; they may pair when more arrive.
  const held = React.useMemo(
    () => packages.filter((pkg) => !isPackageWanted(pkg, paired)),
    [packages, paired]
  )

  /**
   * A completed render is a result for one specific set of settings. Changing
   * format or quality makes it stale, so it is discarded rather than left as a
   * download that no longer matches what the controls say.
   */
  const discardResults = React.useCallback(() => {
    setCharts((current) =>
      current.map((chart) => {
        if (chart.render.status !== "done") return chart
        URL.revokeObjectURL(chart.render.url)
        objectUrls.current.delete(chart.render.url)
        return { ...chart, render: { status: "idle" } }
      })
    )
  }, [])

  const setFormat = React.useCallback(
    (next: OutputFormat) => {
      setFormatState(next)
      discardResults()
    },
    [discardResults]
  )

  const setQuality = React.useCallback(
    (next: Quality) => {
      setQualityState(next)
      discardResults()
    },
    [discardResults]
  )

  const setTracks = React.useCallback(
    (next: Tracks) => {
      // A different track selection is a different render; drop stale results
      // just as format and quality do.
      setTracksState(next)
      discardResults()
    },
    [discardResults]
  )

  const setDifficulty = React.useCallback(
    (id: string, difficulty: Difficulty) => {
      updateChart(id, (chart) => {
        // A different difficulty is a different render; drop the stale result.
        if (chart.render.status === "done") {
          URL.revokeObjectURL(chart.render.url)
          objectUrls.current.delete(chart.render.url)
          return { ...chart, difficulty, render: { status: "idle" } }
        }
        return { ...chart, difficulty }
      })
    },
    [updateChart]
  )

  const renderOne = React.useCallback(
    async (id: string) => {
      // Read from the ref, not the captured `paired`: renderAll awaits between
      // rows, so by the time this runs the closed-over list may describe a
      // queue the user has since changed.
      const target = pairedRef.current.find((chart) => chart.id === id)
      if (!target || !isReady(target)) return

      const difficulty = target.difficulty ?? DEFAULT_DIFFICULTY
      // Pin the settings for this render: switching format mid-run must not
      // mislabel the result or give the blob the wrong MIME type.
      const renderFormat = format
      const renderQuality = quality
      const renderTracks = tracks

      updateChart(id, (chart) => ({
        ...chart,
        render: { status: "rendering", progress: 0 },
      }))

      try {
        const module = await loadRenderOjn()
        // Read both files here rather than at ingest: these buffers are only
        // needed for this render. They are transferred to the worker, not
        // copied, so nothing here may touch them after the call.
        const chartBytes = new Uint8Array(await target.file.arrayBuffer())
        const packageBytes = new Uint8Array(await target.pkg.file.arrayBuffer())
        const { bytes, warnings } = await module.render(
          chartBytes,
          packageBytes,
          difficulty,
          renderFormat,
          renderQuality,
          renderTracks,
          (progress) => {
            updateChart(id, (chart) =>
              chart.render.status === "rendering"
                ? { ...chart, render: { status: "rendering", progress } }
                : chart
            )
          }
        )

        const blob = new Blob([bytes as BlobPart], {
          type: MIME_TYPES[renderFormat],
        })
        const url = URL.createObjectURL(blob)
        objectUrls.current.add(url)

        updateChart(id, (chart) => ({
          ...chart,
          // Diagnostics the core raised — compatibility corrections, ignored
          // directory records. The CLI prints these to stderr; the row shows
          // them so a web render is not quietly less informative.
          warnings,
          render: {
            status: "done",
            url,
            size: bytes.length,
            format: renderFormat,
          },
        }))
        toast.success(`Rendered ${target.info.title}`, {
          description: `${DIFFICULTY_NAMES[difficulty]} · ${renderFormat.toUpperCase()}`,
        })
      } catch (error) {
        const message = messageFrom(error, "The render failed.")
        updateChart(id, (chart) => ({
          ...chart,
          render: { status: "failed", message },
        }))
        toast.error(`Could not render ${target.info.title}`, {
          description: message,
        })
      }
    },
    [format, quality, tracks, updateChart]
  )

  /**
   * Renders every ready row that has no result yet, one at a time.
   *
   * Sequential on purpose: the WASM mix is single-threaded (§7), so running
   * several at once would only contend for the same core and multiply peak
   * memory across sample banks.
   */
  const renderAll = React.useCallback(async () => {
    const isPending = (chart: ChartEntry) =>
      isReady(chart) &&
      chart.render.status !== "done" &&
      chart.render.status !== "rendering"

    // Re-read the queue each pass rather than iterating one snapshot: a row
    // removed mid-batch must not still be rendered, and a row that pairs while
    // the batch runs should be picked up. `done` guards termination — every
    // completed row stops matching, so the loop always drains.
    const attempted = new Set<string>()
    for (;;) {
      const next = pairedRef.current.find(
        (chart) => isPending(chart) && !attempted.has(chart.id)
      )
      if (!next) break
      attempted.add(next.id)
      await renderOne(next.id)
    }
  }, [renderOne])

  const removeChart = React.useCallback((id: string) => {
    setCharts((current) => {
      const target = current.find((chart) => chart.id === id)
      if (target?.render.status === "done") {
        URL.revokeObjectURL(target.render.url)
        objectUrls.current.delete(target.render.url)
      }
      // Packages are kept: another chart may name the same one.
      return current.filter((chart) => chart.id !== id)
    })
  }, [])

  const clearAll = React.useCallback(() => {
    for (const url of objectUrls.current) URL.revokeObjectURL(url)
    objectUrls.current.clear()
    setCharts([])
    setPackages([])
    setRejected([])
  }, [])

  const dismissRejected = React.useCallback(() => setRejected([]), [])

  /**
   * Packs every rendered file into one archive.
   *
   * Reads back through the object URLs rather than keeping a second copy of
   * each render in memory — the blobs already exist, and a queue of WAVs is
   * large enough that holding them twice is worth avoiding.
   */
  const downloadAll = React.useCallback(async () => {
    const done = pairedRef.current.filter(
      (chart) => chart.render.status === "done"
    )
    if (done.length < 2) return

    setIsPacking(true)
    try {
      /*
       * One blob at a time. createZip needs every entry's bytes together, so
       * the archive's own cost is unavoidable -- but reading them through
       * Promise.all briefly doubles that, and a queue of WAV renders is
       * already gigabytes.
       */
      const entries = []
      for (const chart of done) {
        // Narrowed by the filter above; re-read for the type checker.
        const state = chart.render as Extract<
          ChartEntry["render"],
          { status: "done" }
        >
        const blob = await fetch(state.url).then((response) => response.blob())
        entries.push({
          name: downloadName(
            chart,
            DIFFICULTY_NAMES[chart.difficulty ?? DEFAULT_DIFFICULTY],
            state.format
          ),
          bytes: new Uint8Array(await blob.arrayBuffer()),
        })
      }

      const archive = createZip(entries)
      const url = URL.createObjectURL(archive)
      const anchor = document.createElement("a")
      anchor.href = url
      anchor.download = "renderojn.zip"
      anchor.click()
      // Revoked on a later tick: revoking immediately can cancel the download
      // in some browsers before it has read the blob.
      window.setTimeout(() => URL.revokeObjectURL(url), 60_000)
    } catch (error) {
      toast.error("Could not build the zip", {
        description: messageFrom(error, "The archive was not created."),
      })
    } finally {
      setIsPacking(false)
    }
  }, [])

  const readyCount = paired.filter(isReady).length
  const doneCount = paired.filter(
    (chart) => chart.render.status === "done"
  ).length
  const isRendering = paired.some(
    (chart) => chart.render.status === "rendering"
  )

  return {
    // Consumers always see charts with pairing already resolved.
    charts: paired,
    held,
    format,
    setFormat,
    quality,
    setQuality,
    tracks,
    setTracks,
    rejected,
    dismissRejected,
    addFiles,
    setDifficulty,
    renderOne,
    renderAll,
    removeChart,
    clearAll,
    downloadAll,
    isPacking,
    readyCount,
    doneCount,
    isRendering,
  }
}
