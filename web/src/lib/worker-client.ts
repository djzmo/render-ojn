/**
 * Main-thread half of the render worker.
 *
 * Keeps one worker for the whole session — instantiating the module costs a
 * few hundred milliseconds and a couple of hundred megabytes, so a worker per
 * render would be far more expensive than reusing one.
 */
import type { WorkerRequest, WorkerResponse } from "@/workers/render.worker"

import {
  OjnParseError,
  type Difficulty,
  type OjnInfo,
  type OutputFormat,
  type ProgressCallback,
  type Quality,
  type RenderOjnModule,
  type RenderResult,
} from "./renderojn"

interface Pending {
  resolve: (value: { value: unknown; warnings: string[] }) => void
  reject: (error: Error) => void
  onProgress?: ProgressCallback
}

let worker: Worker | null = null
const pending = new Map<number, Pending>()
let nextId = 0

function ensureWorker(): Worker {
  if (worker) return worker

  worker = new Worker(new URL("../workers/render.worker.ts", import.meta.url), {
    type: "module",
  })

  worker.onmessage = (event: MessageEvent<WorkerResponse>) => {
    const message = event.data
    const entry = pending.get(message.id)
    if (!entry) return

    if (message.kind === "progress") {
      entry.onProgress?.(message.fraction)
      return
    }
    pending.delete(message.id)
    if (message.kind === "done") {
      entry.resolve({ value: message.value, warnings: message.warnings })
    } else {
      entry.reject(new OjnParseError(message.message))
    }
  }

  // A worker-level failure (module instantiation, out of memory) never reaches
  // a single request's handler, so fail everything in flight rather than
  // leaving callers hanging on promises that can no longer settle.
  const failAll = (message: string) => {
    for (const [, entry] of pending) entry.reject(new OjnParseError(message))
    pending.clear()
    worker?.terminate()
    worker = null
  }
  worker.onerror = (event) => {
    // Without preventDefault the ErrorEvent keeps propagating and is reported
    // as an uncaught page error too, so a failure the UI already surfaced on
    // the row also looks like a hard crash to the console and any error
    // reporting.
    event.preventDefault()
    failAll(event.message || "The renderer stopped unexpectedly.")
  }
  worker.onmessageerror = () => failAll("The renderer sent an unreadable message.")

  return worker
}

// Omit<> over a union collapses to the fields the members share, dropping the
// render-only ones. Distribute it so each variant keeps its own shape.
type RequestPayload = WorkerRequest extends infer T
  ? T extends { id: number }
    ? Omit<T, "id">
    : never
  : never

function send(
  request: RequestPayload,
  transfer: Transferable[],
  onProgress?: ProgressCallback
): Promise<{ value: unknown; warnings: string[] }> {
  const id = ++nextId
  const active = ensureWorker()
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject, onProgress })
    active.postMessage({ ...request, id } as WorkerRequest, transfer)
  })
}

/*
 * Buffers are transferred, not copied.
 *
 * Transferring detaches the ArrayBuffer on this side, so these methods consume
 * what they are given. That is the right trade now that the queue holds File
 * handles rather than decoded bytes: every caller reads a fresh buffer
 * immediately before the call and drops it immediately after, so copying would
 * allocate a second 30 MB package purely to protect a value nobody reads
 * again — doubling the peak during exactly the operation the lazy-read design
 * exists to keep small.
 *
 * The contract this imposes on callers: pass a buffer you own and will not
 * touch afterwards. Anything that needs the bytes twice must read them twice.
 */
export function createWorkerModule(): RenderOjnModule {
  return {
    async readOjnInfo(bytes: Uint8Array): Promise<OjnInfo> {
      const { value } = await send({ kind: "readOjnInfo", ojn: bytes }, [
        bytes.buffer,
      ])
      return value as OjnInfo
    },

    async render(
      ojn: Uint8Array,
      ojm: Uint8Array,
      difficulty: Difficulty,
      format: OutputFormat,
      quality: Quality,
      onProgress: ProgressCallback
    ): Promise<RenderResult> {
      const { value, warnings } = await send(
        { kind: "render", ojn, ojm, difficulty, format, quality },
        [ojn.buffer, ojm.buffer],
        onProgress
      )
      return { bytes: value as Uint8Array, warnings }
    },
  }
}
