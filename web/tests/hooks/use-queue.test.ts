// @vitest-environment jsdom
import { act, renderHook, waitFor } from "@testing-library/react"
import { afterEach, beforeEach, expect, test, vi } from "vitest"

import type {
  Difficulty,
  OjnInfo,
  OutputFormat,
  ProgressCallback,
  Quality,
  RenderResult,
} from "@/lib/renderojn"

import { useQueue } from "@/hooks/use-queue"
import { makeOjnInfo } from "../factories"

/*
 * The three bugs this file exists to prevent were all sequencing bugs in state
 * updates, so the tests drive the real hook rather than an extracted reducer --
 * pulling the logic out to test it purely would move the defect out of the
 * tested surface.
 */

interface Deferred {
  promise: Promise<RenderResult>
  resolve: (bytes?: Uint8Array) => void
  reject: (error: Error) => void
  onProgress: ProgressCallback
}

/** Records every call and lets a test settle each render on demand. */
const fake = {
  infoFor: new Map<string, OjnInfo>(),
  parseErrorFor: new Map<string, Error>(),
  renderCalls: [] as {
    ojn: Uint8Array
    difficulty: Difficulty
    format: OutputFormat
    quality: Quality
  }[],
  pending: [] as Deferred[],
  /** When false, renders settle immediately -- the default for most tests. */
  deferRenders: false,
  /** Concurrency counters, for the sequential-ingest regression tests. */
  parsesInFlight: 0,
  peakParsesInFlight: 0,
  /** Bytes resident in overlapping File reads, and the high-water mark. */
  bytesInFlight: 0,
  peakBytesInFlight: 0,
}

/**
 * Makes File.arrayBuffer observable.
 *
 * Ingest reads packages that are megabytes each, so the number resident at
 * once is the property that decides whether a folder import survives. Wrapping
 * the read is the only way a test can see it: the returned Uint8Arrays look
 * identical however many were built in parallel.
 */
function instrumentFileReads() {
  const original = File.prototype.arrayBuffer
  File.prototype.arrayBuffer = async function instrumented(this: File) {
    fake.bytesInFlight += this.size
    fake.peakBytesInFlight = Math.max(fake.peakBytesInFlight, fake.bytesInFlight)
    try {
      // Yield so genuinely concurrent readers overlap here.
      await Promise.resolve()
      return await original.call(this)
    } finally {
      fake.bytesInFlight -= this.size
    }
  }
  return () => {
    File.prototype.arrayBuffer = original
  }
}

function keyOf(bytes: Uint8Array): string {
  return Array.from(bytes.slice(0, 4)).join(",")
}

vi.mock("@/lib/renderojn", async (importOriginal) => {
  // Override only loadRenderOjn. The hook also imports DIFFICULTY_NAMES,
  // MIME_TYPES, and DEFAULT_QUALITY from here; mocking the whole module would
  // silently replace those constants with undefined.
  const actual = await importOriginal<typeof import("@/lib/renderojn")>()
  return {
    ...actual,
    loadRenderOjn: () =>
      Promise.resolve({
        readOjnInfo: async (bytes: Uint8Array) => {
          fake.parsesInFlight += 1
          fake.peakParsesInFlight = Math.max(
            fake.peakParsesInFlight,
            fake.parsesInFlight
          )
          try {
            // Yield so a genuinely parallel caller overlaps here.
            await Promise.resolve()
            const key = keyOf(bytes)
            const failure = fake.parseErrorFor.get(key)
            if (failure) throw failure
            return fake.infoFor.get(key) ?? makeOjnInfo()
          } finally {
            fake.parsesInFlight -= 1
          }
        },
        render: (
          ojn: Uint8Array,
          _ojm: Uint8Array,
          difficulty: Difficulty,
          format: OutputFormat,
          quality: Quality,
          onProgress: ProgressCallback
        ) => {
          fake.renderCalls.push({ ojn, difficulty, format, quality })
          if (!fake.deferRenders) {
            return Promise.resolve({
              bytes: new Uint8Array([1, 2, 3]),
              warnings: [],
            })
          }
          let resolve!: Deferred["resolve"]
          let reject!: Deferred["reject"]
          const promise = new Promise<RenderResult>((res, rej) => {
            resolve = (bytes = new Uint8Array([1, 2, 3])) =>
              res({ bytes, warnings: [] })
            reject = rej
          })
          fake.pending.push({ promise, resolve, reject, onProgress })
          return promise
        },
      }),
  }
})

vi.mock("sonner", () => ({
  toast: { success: vi.fn(), error: vi.fn() },
}))

let createdUrls: string[] = []
let revokedUrls: string[] = []

beforeEach(() => {
  fake.infoFor.clear()
  fake.parseErrorFor.clear()
  fake.renderCalls = []
  fake.pending = []
  fake.deferRenders = false
  fake.parsesInFlight = 0
  fake.peakParsesInFlight = 0
  fake.bytesInFlight = 0
  fake.peakBytesInFlight = 0
  createdUrls = []
  revokedUrls = []

  // Assigned, not spied: jsdom leaves createObjectURL unimplemented, so a spy
  // that ever gets restored would throw on the next mount. Plain assignment
  // survives clearAllMocks and stays stable across the file.
  let urlCounter = 0
  URL.createObjectURL = (() => {
    const url = `blob:mock/${++urlCounter}`
    createdUrls.push(url)
    return url
  }) as typeof URL.createObjectURL
  URL.revokeObjectURL = ((url: string) => {
    revokedUrls.push(url)
  }) as typeof URL.revokeObjectURL

  vi.stubGlobal(
    "fetch",
    vi.fn(async () => ({ blob: async () => new Blob([new Uint8Array([9])]) }))
  )
  vi.spyOn(HTMLAnchorElement.prototype, "click").mockImplementation(() => {})
})

afterEach(async () => {
  // Settle anything a test left deferred. An unresolved render promise keeps
  // renderAll's loop alive into the next test, where it renders against a
  // torn-down hook and leaves result.current null for everything after it.
  for (const entry of fake.pending) entry.resolve()
  fake.pending = []
  await act(async () => {
    await Promise.resolve()
  })

  vi.unstubAllGlobals()
  // Deliberately not restoreAllMocks(): that puts back jsdom's own
  // URL.createObjectURL, which is unimplemented and throws, so every test after
  // the first would fail on mount. beforeEach re-establishes the spies anyway.
  vi.clearAllMocks()
})

/** A File whose first bytes identify it to the fake module. */
function chartFile(name: string, marker: number): File {
  return new File([new Uint8Array([marker, marker, marker, marker])], name)
}

/**
 * A package sized like a real one.
 *
 * Sample packages in the reference corpus run 3.7-30 MB. Tests that use
 * 4-byte stand-ins cannot observe anything about how ingest handles their
 * bulk, which is exactly how concurrent reads of a whole folder reached users
 * as an Out of Memory crash.
 */
function bigPackageFile(name: string, megabytes: number): File {
  return new File([new Uint8Array(megabytes * 1024 * 1024)], name)
}

function packageFile(name: string): File {
  return new File([new Uint8Array([200, 200, 200, 200])], name)
}

/*
 * Statically imported: a dynamic import after vi.resetModules() gives the hook
 * a second copy of React, and renderHook then never commits (result.current
 * stays null). The hook's module-level id counter therefore persists across
 * tests, which is why nothing here asserts on literal ids.
 */
function loadQueue() {
  return renderHook(() => useQueue())
}

/** Drops a chart and its package, and waits for the row to become ready. */
async function seedReadyChart(
  view: ReturnType<typeof loadQueue>,
  marker: number,
  packageName: string
) {
  fake.infoFor.set(`${marker},${marker},${marker},${marker}`, makeOjnInfo({ packageName }))
  await act(async () => {
    await view.result.current.addFiles([
      chartFile(`song${marker}.ojn`, marker),
      packageFile(packageName),
    ])
  })
  await waitFor(() => expect(view.result.current.readyCount).toBeGreaterThan(0))
}

/* ------------------------------------------------------------------ *
 * renderAll: termination and freshness
 * ------------------------------------------------------------------ */

test("renderAll stops after a row fails instead of retrying it forever", async () => {
  // A failed row keeps matching isPending, so termination rests entirely on the
  // `attempted` set. Without it this loops until the test times out.
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")

  fake.deferRenders = true
  // Started outside act and awaited at the end: wrapping an un-awaited call in
  // act interleaves act scopes, which React warns about and which makes the
  // assertions below race the loop.
  const batch = view.result.current.renderAll()
  await waitFor(() => expect(fake.pending).toHaveLength(1))

  await act(async () => {
    fake.pending[0].reject(new Error("forced failure"))
    await batch
  })

  expect(fake.renderCalls).toHaveLength(1)
  expect(view.result.current.charts[0].render.status).toBe("failed")
})

test("renderAll skips a row removed while the batch is running", async () => {
  // The historical bug: renderAll iterated a snapshot captured when the button
  // was clicked, so a row deleted mid-batch was still rendered and its result
  // thrown away.
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  await seedReadyChart(view, 2, "b.ojm")
  await waitFor(() => expect(view.result.current.readyCount).toBe(2))

  fake.deferRenders = true
  const second = view.result.current.charts[1]

  const batch = view.result.current.renderAll()
  await waitFor(() => expect(fake.pending).toHaveLength(1))

  // Delete the queued row while the first one is still in flight.
  act(() => view.result.current.removeChart(second.id))

  await act(async () => {
    fake.pending[0].resolve()
    await batch
  })

  expect(fake.renderCalls).toHaveLength(1)
})

test("renderAll picks up a chart that becomes ready during the batch", async () => {
  // The upside of reading current state each pass, which would regress silently
  // if someone reverted to iterating a snapshot.
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")

  fake.infoFor.set("2,2,2,2", makeOjnInfo({ packageName: "late.ojm" }))
  await act(async () => {
    await view.result.current.addFiles([chartFile("song2.ojn", 2)])
  })
  await waitFor(() => expect(view.result.current.charts).toHaveLength(2))
  expect(view.result.current.readyCount).toBe(1)

  fake.deferRenders = true
  const batch = view.result.current.renderAll()
  await waitFor(() => expect(fake.pending).toHaveLength(1))

  // The missing package arrives while row 1 is still rendering.
  await act(async () => {
    await view.result.current.addFiles([packageFile("late.ojm")])
  })

  await act(async () => {
    fake.pending[0].resolve()
    await Promise.resolve()
  })
  // The loop re-reads the queue, so row 2 is now pending too.
  await waitFor(() => expect(fake.pending).toHaveLength(2))

  await act(async () => {
    fake.pending[1].resolve()
    await batch
  })

  await waitFor(() => expect(fake.renderCalls).toHaveLength(2))
})

/* ------------------------------------------------------------------ *
 * Rejected files
 * ------------------------------------------------------------------ */

test("a second drop keeps the skipped filenames from the first", async () => {
  // The historical bug: setRejected replaced the list, so the first drop's
  // names vanished and those files looked accepted.
  const view = loadQueue()

  await act(async () => {
    await view.result.current.addFiles([new File([new Uint8Array(4)], "readme.txt")])
  })
  expect(view.result.current.rejected).toEqual(["readme.txt"])

  await act(async () => {
    await view.result.current.addFiles([new File([new Uint8Array(4)], "cover.png")])
  })
  expect(view.result.current.rejected).toEqual(["readme.txt", "cover.png"])
})

test("re-dropping the same unsupported file does not list it twice", async () => {
  const view = loadQueue()
  const drop = () =>
    act(async () => {
      await view.result.current.addFiles([new File([new Uint8Array(4)], "readme.txt")])
    })

  await drop()
  await drop()
  expect(view.result.current.rejected).toEqual(["readme.txt"])
})

test("dismissing the notice clears every skipped filename", async () => {
  const view = loadQueue()
  await act(async () => {
    await view.result.current.addFiles([new File([new Uint8Array(4)], "a.txt")])
  })
  act(() => view.result.current.dismissRejected())
  expect(view.result.current.rejected).toEqual([])
})

/* ------------------------------------------------------------------ *
 * Settings reaching the module
 * ------------------------------------------------------------------ */

test("the quality sent to the module is the CLI scale, not a zero-based index", async () => {
  // The historical bug lived in the constants, but an off-by-one could just as
  // easily be reintroduced at this call site.
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")

  await act(async () => {
    await view.result.current.renderOne(view.result.current.charts[0].id)
  })
  expect(fake.renderCalls[0].quality).toBe(3)

  act(() => view.result.current.setQuality(1))
  await act(async () => {
    await view.result.current.renderOne(view.result.current.charts[0].id)
  })
  expect(fake.renderCalls[1].quality).toBe(1)
})

test("a render keeps the format that was current when it started", async () => {
  // Switching format mid-render must not mislabel the finished file or give the
  // blob the wrong MIME type.
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")

  fake.deferRenders = true
  const id = view.result.current.charts[0].id
  const run = view.result.current.renderOne(id)
  await waitFor(() => expect(fake.pending).toHaveLength(1))

  act(() => view.result.current.setFormat("wav"))
  await act(async () => {
    fake.pending[0].resolve()
    await run
  })

  expect(fake.renderCalls[0].format).toBe("ogg")
})

test("difficulty defaults to Hard when the chart has it", async () => {
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  expect(view.result.current.charts[0].difficulty).toBe(2)
})

test("difficulty falls back to the highest available when Hard is absent", async () => {
  const view = loadQueue()
  fake.infoFor.set(
    "1,1,1,1",
    makeOjnInfo({
      packageName: "a.ojm",
      difficulties: [{ difficulty: 0, noteCount: 100, durationSeconds: 60 }],
    })
  )
  await act(async () => {
    await view.result.current.addFiles([chartFile("song1.ojn", 1), packageFile("a.ojm")])
  })
  await waitFor(() => expect(view.result.current.charts[0].info).toBeDefined())
  expect(view.result.current.charts[0].difficulty).toBe(0)
})

/* ------------------------------------------------------------------ *
 * Object URL lifetime
 * ------------------------------------------------------------------ */

test("changing the format discards finished results and revokes their URLs", async () => {
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  await act(async () => {
    await view.result.current.renderOne(view.result.current.charts[0].id)
  })
  await waitFor(() => expect(view.result.current.doneCount).toBe(1))
  expect(createdUrls).toHaveLength(1)

  act(() => view.result.current.setFormat("mp3"))

  expect(revokedUrls).toEqual(createdUrls)
  expect(view.result.current.charts[0].render.status).toBe("idle")
})

test("removing a rendered chart revokes its object URL", async () => {
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  await act(async () => {
    await view.result.current.renderOne(view.result.current.charts[0].id)
  })
  await waitFor(() => expect(view.result.current.doneCount).toBe(1))

  act(() => view.result.current.removeChart(view.result.current.charts[0].id))
  expect(revokedUrls).toEqual(createdUrls)
})

test("clearing the queue revokes every outstanding object URL", async () => {
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  await seedReadyChart(view, 2, "b.ojm")
  await act(async () => {
    await view.result.current.renderAll()
  })
  await waitFor(() => expect(view.result.current.doneCount).toBe(2))

  act(() => view.result.current.clearAll())
  expect(revokedUrls.sort()).toEqual(createdUrls.sort())
  expect(view.result.current.charts).toEqual([])
})

/* ------------------------------------------------------------------ *
 * Parsing and pairing
 * ------------------------------------------------------------------ */

test("a chart whose header fails to parse shows the error without blocking others", async () => {
  const view = loadQueue()
  fake.parseErrorFor.set("1,1,1,1", new Error("File is too small"))
  fake.infoFor.set("2,2,2,2", makeOjnInfo({ packageName: "b.ojm" }))

  await act(async () => {
    await view.result.current.addFiles([
      chartFile("bad.ojn", 1),
      chartFile("good.ojn", 2),
      packageFile("b.ojm"),
    ])
  })

  await waitFor(() => expect(view.result.current.charts).toHaveLength(2))
  const [bad, good] = view.result.current.charts
  expect(bad.parseError).toBe("File is too small")
  expect(good.info).toBeDefined()
  expect(view.result.current.readyCount).toBe(1)
})

test("a chart and its package dropped together pair on the same drop", async () => {
  // Packages are loaded into state before charts precisely so this works.
  const view = loadQueue()
  await seedReadyChart(view, 1, "together.ojm")
  expect(view.result.current.charts[0].pkg).toBeDefined()
  expect(view.result.current.held).toEqual([])
})

test("dropping many charts parses them one at a time", async () => {
  /*
   * Parsing in parallel does not finish sooner -- the worker handles messages
   * serially -- but it does queue a copy of every chart in the worker's inbox
   * at once, and leaves the later rows sitting as skeletons until the backlog
   * drains. On a folder of real songs that showed up as half the queue hanging,
   * and combined with reading every sample package concurrently it pushed the
   * tab into Out of Memory.
   */
  const view = loadQueue()
  const files = Array.from({ length: 6 }, (_, index) => {
    fake.infoFor.set(
      `${index},${index},${index},${index}`,
      makeOjnInfo({ packageName: `pkg${index}.ojm` })
    )
    return chartFile(`song${index}.ojn`, index)
  })

  await act(async () => {
    await view.result.current.addFiles(files)
  })

  await waitFor(() => expect(view.result.current.charts).toHaveLength(6))
  expect(view.result.current.charts.every((chart) => chart.info)).toBe(true)
  expect(fake.peakParsesInFlight).toBe(1)
})

test("importing a folder of songs holds one package in memory at a time", async () => {
  /*
   * The primary use case. Dropping a whole folder is what the empty state
   * invites, and sample packages run 3.7-30 MB each, so how many are resident
   * at once decides whether the import survives. Reading them concurrently put
   * every package in memory before a single render had allocated anything,
   * which reached users as an Out of Memory crash on a mid-sized folder.
   *
   * Asserted as a bound on overlapping reads rather than on heap size: heap is
   * not observable across environments, and the overlap is the property that
   * actually causes the growth.
   */
  const restore = instrumentFileReads()
  try {
    const view = loadQueue()
    const megabytes = 8
    const songs = 8

    const files: File[] = []
    for (let index = 0; index < songs; index++) {
      fake.infoFor.set(
        `${index},${index},${index},${index}`,
        makeOjnInfo({ packageName: `song${index}.ojm` })
      )
      files.push(chartFile(`song${index}.ojn`, index))
      files.push(bigPackageFile(`song${index}.ojm`, megabytes))
    }

    await act(async () => {
      await view.result.current.addFiles(files)
    })
    await waitFor(() => expect(view.result.current.readyCount).toBe(songs))

    // One package at a time, not all eight: the peak must stay near a single
    // file rather than scaling with how many were dropped.
    const onePackage = megabytes * 1024 * 1024
    expect(fake.peakBytesInFlight).toBeLessThanOrEqual(onePackage * 2)
    expect(fake.peakBytesInFlight).toBeLessThan(songs * onePackage)

    /*
     * And nothing is retained afterwards. This is the part that actually
     * decides whether a large import survives: bounding the read *spike* still
     * left every package's bytes sitting in queue state for the life of the
     * session, so a big enough folder ran the tab out of memory however
     * carefully the files were read. Entries hold File handles, which are
     * references to data the browser keeps on disk.
     */
    for (const chart of view.result.current.charts) {
      expect(chart.file).toBeInstanceOf(File)
      expect(chart).not.toHaveProperty("bytes")
      expect(chart.pkg?.file).toBeInstanceOf(File)
      expect(chart.pkg).not.toHaveProperty("bytes")
    }
  } finally {
    restore()
  }
})

test("a file that cannot be read reports the reason the browser gave", async () => {
  /*
   * Files are read when a row parses, not when it is dropped, so the window
   * between the two is however long the queue sits -- long enough for a user
   * to move or delete a file. The browser rejects with a DOMException, which
   * is not an Error instance, so narrowing on `instanceof Error` replaced
   * "file moved" with a generic string the user could not tell apart from a
   * corrupt chart.
   */
  const original = File.prototype.arrayBuffer
  File.prototype.arrayBuffer = () =>
    Promise.reject(new DOMException("file moved", "NotReadableError"))
  try {
    const view = loadQueue()
    await act(async () => {
      await view.result.current.addFiles([chartFile("gone.ojn", 1)])
    })
    await waitFor(() => expect(view.result.current.charts).toHaveLength(1))
    expect(view.result.current.charts[0].parseError).toBe("file moved")
  } finally {
    File.prototype.arrayBuffer = original
  }
})

test("a package nothing names is held rather than shown as a row", async () => {
  const view = loadQueue()
  await act(async () => {
    await view.result.current.addFiles([packageFile("orphan.ojm")])
  })
  expect(view.result.current.charts).toEqual([])
  expect(view.result.current.held).toHaveLength(1)
})

/* ------------------------------------------------------------------ *
 * downloadAll
 * ------------------------------------------------------------------ */

test("downloadAll does nothing with fewer than two finished renders", async () => {
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  await act(async () => {
    await view.result.current.renderOne(view.result.current.charts[0].id)
  })
  await waitFor(() => expect(view.result.current.doneCount).toBe(1))

  const before = createdUrls.length
  await act(async () => {
    await view.result.current.downloadAll()
  })
  // No archive URL was minted, so nothing was packed.
  expect(createdUrls).toHaveLength(before)
})

test("downloadAll packs every finished render into one archive", async () => {
  const view = loadQueue()
  await seedReadyChart(view, 1, "a.ojm")
  await seedReadyChart(view, 2, "b.ojm")
  await act(async () => {
    await view.result.current.renderAll()
  })
  await waitFor(() => expect(view.result.current.doneCount).toBe(2))

  const before = createdUrls.length
  await act(async () => {
    await view.result.current.downloadAll()
  })
  // One additional URL: the zip itself.
  expect(createdUrls).toHaveLength(before + 1)
  expect(HTMLAnchorElement.prototype.click).toHaveBeenCalled()
})
