/**
 * Types for the Emscripten glue in this directory.
 *
 * `renderojn.js` is a build artifact (see scripts/sync-wasm.mjs), so it ships
 * no types of its own. Only the factory is declared here — the module's own
 * surface is typed at the worker boundary, which is the only place that
 * touches it.
 */
declare module "@/wasm/renderojn.js" {
  interface CreateOptions {
    locateFile?: (path: string, scriptDirectory: string) => string
    wasmBinary?: ArrayBuffer | Uint8Array
    print?: (text: string) => void
    printErr?: (text: string) => void
  }
  const createRenderOJN: (options?: CreateOptions) => Promise<unknown>
  export default createRenderOJN
}
