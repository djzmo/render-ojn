/// <reference lib="webworker" />
/**
 * Owns the WebAssembly module.
 *
 * The mix is single-threaded and synchronous — GitHub Pages cannot send the
 * COOP/COEP headers that WASM threads require, so there is no way to split one
 * render across cores. Running it here instead of on the
 * main thread is what keeps the page responsive while a render is in flight.
 *
 * Protocol: one request in, progress messages out, then exactly one `done` or
 * `error`. Requests are serialized by `id` so several rows can be queued
 * without their progress interleaving.
 */
import createRenderOJN from "@/wasm/renderojn.js"
import wasmUrl from "@/wasm/renderojn.wasm?url"

import {
  FORMAT_VALUES,
  RENDER_MODE_VALUES,
  TRACKS_VALUES,
  type Difficulty,
  type OutputFormat,
  type Quality,
  type RenderMode,
  type Tracks,
} from "@/lib/renderojn"

interface EmscriptenModule {
  readOjnInfo(bytes: Uint8Array): RawOjnInfo
  render(
    ojn: Uint8Array,
    ojm: Uint8Array,
    difficulty: number,
    format: number,
    quality: number,
    tracks: number,
    renderMode: number,
    onProgress: ((fraction: number) => void) | undefined
  ): RawRenderResult
  getExceptionMessage?(pointer: number): string[]
}

/** embind returns std::vector as an opaque handle, not a JS array. */
interface RawVector<T> {
  size(): number
  get(index: number): T
  delete?(): void
}

interface RawDifficultyInfo {
  difficulty: number
  noteCount: number
  durationSeconds: number
}

interface RawOjnInfo {
  title: string
  artist: string
  charter: string
  packageName: string
  genre: string
  songId: number
  tempo: number
  difficulties: RawVector<RawDifficultyInfo>
  warnings: RawVector<string>
}

interface RawRenderResult {
  bytes: Uint8Array
  warnings: RawVector<string>
}

export type WorkerRequest =
  | { id: number; kind: "readOjnInfo"; ojn: Uint8Array }
  | {
      id: number
      kind: "render"
      ojn: Uint8Array
      ojm: Uint8Array
      difficulty: Difficulty
      format: OutputFormat
      quality: Quality
      tracks: Tracks
      renderMode: RenderMode
    }

export type WorkerResponse =
  | { id: number; kind: "progress"; fraction: number }
  | { id: number; kind: "done"; value: unknown; warnings: string[] }
  | { id: number; kind: "error"; message: string }

let modulePromise: Promise<EmscriptenModule> | null = null

function loadModule(): Promise<EmscriptenModule> {
  if (!modulePromise) {
    // locateFile points the glue at the hashed asset URL Vite emitted; without
    // it the module would fetch "renderojn.wasm" relative to the document.
    modulePromise = createRenderOJN({
      locateFile: (file: string) => (file.endsWith(".wasm") ? wasmUrl : file),
    }) as Promise<EmscriptenModule>
  }
  return modulePromise
}

function toArray<T>(vector: RawVector<T>): T[] {
  const items: T[] = []
  for (let index = 0; index < vector.size(); ++index) items.push(vector.get(index))
  // embind hands out heap-backed handles; releasing them keeps a long queue
  // from growing the WASM heap one render at a time.
  vector.delete?.()
  return items
}

/**
 * Turns whatever the module threw into a readable string.
 *
 * The binding translates core errors into real JS Errors before they cross
 * (see `with_js_errors` in `src/wasm/Bindings.cpp`), so `error.message` is the
 * normal path. The `excPtr` branch remains only as a guard: an exception
 * escaping some other way would otherwise stringify as "[object Object]".
 */
function explain(module: EmscriptenModule | null, error: unknown): string {
  if (error instanceof Error && error.message) return error.message
  if (error && typeof error === "object" && "excPtr" in error) {
    const pointer = (error as { excPtr: number }).excPtr
    try {
      const parts = module?.getExceptionMessage?.(pointer)
      if (parts?.length) return parts.filter(Boolean).join(": ")
    } catch {
      // getExceptionMessage faults on some pointers; fall through.
    }
    return "The renderer could not process this file."
  }
  return String(error)
}

self.onmessage = async (event: MessageEvent<WorkerRequest>) => {
  const request = event.data
  const post = (message: WorkerResponse, transfer?: Transferable[]) =>
    (self as unknown as Worker).postMessage(message, transfer ?? [])

  let module: EmscriptenModule | null = null
  try {
    module = await loadModule()

    if (request.kind === "readOjnInfo") {
      const raw = module.readOjnInfo(request.ojn)
      const value = {
        title: raw.title,
        artist: raw.artist,
        charter: raw.charter,
        packageName: raw.packageName,
        genre: raw.genre,
        songId: raw.songId,
        tempo: raw.tempo,
        difficulties: toArray(raw.difficulties).map((entry) => ({
          difficulty: entry.difficulty as Difficulty,
          noteCount: entry.noteCount,
          durationSeconds: entry.durationSeconds,
        })),
      }
      post({ id: request.id, kind: "done", value, warnings: toArray(raw.warnings) })
      return
    }

    const result = module.render(
      request.ojn,
      request.ojm,
      request.difficulty,
      FORMAT_VALUES[request.format],
      request.quality,
      TRACKS_VALUES[request.tracks],
      RENDER_MODE_VALUES[request.renderMode],
      (fraction) => post({ id: request.id, kind: "progress", fraction })
    )

    // Copy out of the module's view before transferring: the returned array is
    // already a JS-owned slice, but its buffer may be shared with the heap view.
    const bytes = new Uint8Array(result.bytes)
    post({ id: request.id, kind: "done", value: bytes, warnings: toArray(result.warnings) }, [
      bytes.buffer,
    ])
  } catch (error) {
    post({ id: request.id, kind: "error", message: explain(module, error) })
  }
}
