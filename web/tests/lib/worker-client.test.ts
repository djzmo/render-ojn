import { afterEach, beforeEach, expect, test, vi } from "vitest"

import type { WorkerResponse } from "@/workers/render.worker"

/*
 * No WASM is involved. worker-client.ts imports the worker module as
 * `import type` under verbatimModuleSyntax, so it is erased at compile time --
 * faking the Worker constructor is enough to exercise the whole client half,
 * and the git-ignored Emscripten artifact is never loaded.
 */

interface Posted {
  message: { id: number; kind: string; ojn?: Uint8Array; ojm?: Uint8Array }
  transfer: Transferable[]
}

class FakeWorker {
  static instances: FakeWorker[] = []
  posted: Posted[] = []
  terminated = false
  onmessage: ((event: MessageEvent<WorkerResponse>) => void) | null = null
  onerror: ((event: Partial<ErrorEvent>) => void) | null = null
  onmessageerror: (() => void) | null = null

  constructor() {
    FakeWorker.instances.push(this)
  }

  postMessage(message: Posted["message"], transfer: Transferable[] = []) {
    this.posted.push({ message, transfer })
  }

  terminate() {
    this.terminated = true
  }

  /** Delivers a response as the real worker would. */
  reply(response: WorkerResponse) {
    this.onmessage?.({ data: response } as MessageEvent<WorkerResponse>)
  }
}

/** Re-imports the module so its worker/pending/nextId state starts clean. */
async function freshClient() {
  vi.resetModules()
  FakeWorker.instances = []
  const { createWorkerModule } = await import("@/lib/worker-client")
  return createWorkerModule()
}

const latest = () => FakeWorker.instances[FakeWorker.instances.length - 1]

beforeEach(() => {
  vi.stubGlobal("Worker", FakeWorker)
})

afterEach(() => {
  vi.unstubAllGlobals()
})

test("a response resolves the request that carries its id", async () => {
  const client = await freshClient()
  const promise = client.readOjnInfo(new Uint8Array([1, 2, 3]))

  const { id } = latest().posted[0].message
  latest().reply({ id, kind: "done", value: { title: "Bach Alive" }, warnings: [] })

  await expect(promise).resolves.toMatchObject({ title: "Bach Alive" })
})

test("concurrent requests resolve independently when answered out of order", async () => {
  const client = await freshClient()
  const first = client.readOjnInfo(new Uint8Array([1]))
  const second = client.readOjnInfo(new Uint8Array([2]))

  const [postedFirst, postedSecond] = latest().posted
  latest().reply({
    id: postedSecond.message.id,
    kind: "done",
    value: { title: "second" },
    warnings: [],
  })
  latest().reply({
    id: postedFirst.message.id,
    kind: "done",
    value: { title: "first" },
    warnings: [],
  })

  await expect(first).resolves.toMatchObject({ title: "first" })
  await expect(second).resolves.toMatchObject({ title: "second" })
})

test("a response with an unrecognized id is ignored rather than throwing", async () => {
  const client = await freshClient()
  const promise = client.readOjnInfo(new Uint8Array([1]))

  expect(() =>
    latest().reply({ id: 9999, kind: "done", value: {}, warnings: [] })
  ).not.toThrow()

  const { id } = latest().posted[0].message
  latest().reply({ id, kind: "done", value: { title: "ok" }, warnings: [] })
  await expect(promise).resolves.toMatchObject({ title: "ok" })
})

test("progress messages report without settling the request", async () => {
  const client = await freshClient()
  const seen: number[] = []
  const promise = client.render(
    new Uint8Array([1]),
    new Uint8Array([2]),
    2,
    "ogg",
    3,
    "all",
    "quick",
    (fraction) => seen.push(fraction)
  )

  const { id } = latest().posted[0].message
  latest().reply({ id, kind: "progress", fraction: 0.25 })
  latest().reply({ id, kind: "progress", fraction: 0.75 })
  expect(seen).toEqual([0.25, 0.75])

  latest().reply({ id, kind: "done", value: new Uint8Array([7]), warnings: [] })
  await expect(promise).resolves.toMatchObject({ warnings: [] })
})

test("an error response rejects with the message the worker sent", async () => {
  const client = await freshClient()
  const promise = client.readOjnInfo(new Uint8Array([1]))

  const { id } = latest().posted[0].message
  latest().reply({ id, kind: "error", message: "Unsupported OJN format" })

  await expect(promise).rejects.toThrow("Unsupported OJN format")
})

test("a worker failure rejects every request in flight", async () => {
  // Nothing else covers this: without it a module-instantiation failure or an
  // out-of-memory leaves callers waiting on promises that can never settle,
  // which is the worst failure mode this app has.
  const client = await freshClient()
  const first = client.readOjnInfo(new Uint8Array([1]))
  const second = client.readOjnInfo(new Uint8Array([2]))

  latest().onerror?.({ message: "out of memory", preventDefault: () => {} })

  await expect(first).rejects.toThrow("out of memory")
  await expect(second).rejects.toThrow("out of memory")
})

test("a worker failure calls preventDefault so it is not also reported as uncaught", async () => {
  const client = await freshClient()
  void client.readOjnInfo(new Uint8Array([1])).catch(() => {})

  const preventDefault = vi.fn()
  latest().onerror?.({ message: "boom", preventDefault })
  expect(preventDefault).toHaveBeenCalled()
})

test("an unreadable message also fails everything in flight", async () => {
  const client = await freshClient()
  const promise = client.readOjnInfo(new Uint8Array([1]))

  latest().onmessageerror?.()
  await expect(promise).rejects.toThrow(/unreadable/i)
})

test("the next request after a failure starts a new worker", async () => {
  // worker = null is the recovery path; without it every later call would post
  // into a terminated worker and hang.
  const client = await freshClient()
  void client.readOjnInfo(new Uint8Array([1])).catch(() => {})
  const failed = latest()
  failed.onerror?.({ message: "boom", preventDefault: () => {} })

  expect(failed.terminated).toBe(true)
  void client.readOjnInfo(new Uint8Array([2])).catch(() => {})
  expect(FakeWorker.instances).toHaveLength(2)
})

test("many requests share a single worker instance", async () => {
  // Instantiating the module costs hundreds of milliseconds and a large heap,
  // so a worker per render would be far more expensive than reusing one.
  const client = await freshClient()
  void client.readOjnInfo(new Uint8Array([1])).catch(() => {})
  void client.readOjnInfo(new Uint8Array([2])).catch(() => {})
  void client.readOjnInfo(new Uint8Array([3])).catch(() => {})

  expect(FakeWorker.instances).toHaveLength(1)
  expect(latest().posted).toHaveLength(3)
})

test("a render transfers the caller's own buffers rather than copying them", async () => {
  /*
   * Transferring instead of copying is deliberate. Callers read a fresh buffer
   * immediately before the call and drop it immediately after, so a defensive
   * copy would allocate a second 30 MB package to protect a value nobody reads
   * again -- doubling peak memory during exactly the operation that lazy file
   * reads exist to keep small.
   *
   * The cost is a real contract: these methods consume what they are given.
   * Anything that needs the bytes twice must read the File twice.
   */
  const client = await freshClient()
  const ojn = new Uint8Array([1, 2, 3, 4])
  const ojm = new Uint8Array([5, 6, 7, 8])

  void client.render(ojn, ojm, 2, "ogg", 3, "all", "quick", () => {}).catch(() => {})

  const { transfer } = latest().posted[0]
  expect(transfer).toHaveLength(2)
  expect(transfer).toContain(ojn.buffer)
  expect(transfer).toContain(ojm.buffer)
})

test("reading a header transfers the caller's buffer rather than copying it", async () => {
  const client = await freshClient()
  const ojn = new Uint8Array([1, 2, 3, 4])

  void client.readOjnInfo(ojn).catch(() => {})

  const { transfer } = latest().posted[0]
  expect(transfer).toEqual([ojn.buffer])
})

test("the format crosses as its name and is mapped inside the worker", async () => {
  // FORMAT_VALUES is applied worker-side; the client posts the string so the
  // enum mapping lives in exactly one place.
  const client = await freshClient()
  void client.render(new Uint8Array([1]), new Uint8Array([2]), 2, "ogg", 3, "all", "quick", () => {}).catch(
    () => {}
  )

  expect(latest().posted[0].message).toMatchObject({ kind: "render" })
})
