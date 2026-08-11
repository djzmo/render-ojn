import { expect, test } from "vitest"

import { dragHasFiles, filesFromDrop } from "@/lib/drop"

/*
 * DataTransfer and FileSystemEntry are only ever read here, never constructed,
 * so plain object literals exercise the whole module -- no DOM required.
 */

function fileEntry(name: string, options: { fails?: boolean } = {}) {
  return {
    isFile: true,
    isDirectory: false,
    file: (
      onSuccess: (file: File) => void,
      onError: (error: Error) => void
    ) => {
      if (options.fails) onError(new Error("unreadable"))
      else onSuccess(new File([new Uint8Array([1])], name))
    },
  }
}

/** A directory whose reader hands back `children` in 100-entry batches. */
function directoryEntry(children: unknown[]) {
  let offset = 0
  return {
    isFile: false,
    isDirectory: true,
    createReader: () => ({
      readEntries: (onSuccess: (entries: unknown[]) => void) => {
        const batch = children.slice(offset, offset + 100)
        offset += batch.length
        onSuccess(batch)
      },
    }),
  }
}

function transfer(options: {
  entries?: unknown[]
  files?: File[]
  types?: string[]
}): DataTransfer {
  return {
    types: options.types ?? ["Files"],
    items: (options.entries ?? []).map((entry) => ({
      webkitGetAsEntry: () => entry,
    })),
    files: options.files ?? [],
  } as unknown as DataTransfer
}

test("a drag carrying files is recognized", () => {
  expect(dragHasFiles(transfer({ types: ["Files"] }))).toBe(true)
})

test("a drag carrying only text is not treated as files", () => {
  // Dragging a text selection across the window must not light up the page as
  // though it were about to accept something.
  expect(dragHasFiles(transfer({ types: ["text/plain"] }))).toBe(false)
  expect(dragHasFiles(null)).toBe(false)
})

test("a plain file drop yields its files", async () => {
  const files = await filesFromDrop(
    transfer({ entries: [fileEntry("a.ojn"), fileEntry("b.ojm")] })
  )
  expect(files.map((file) => file.name)).toEqual(["a.ojn", "b.ojm"])
})

test("a browser without entry support falls back to the file list", async () => {
  const fallback = new File([new Uint8Array([1])], "fallback.ojn")
  const dataTransfer = {
    types: ["Files"],
    items: [{ webkitGetAsEntry: undefined }],
    files: [fallback],
  } as unknown as DataTransfer

  const files = await filesFromDrop(dataTransfer)
  expect(files).toEqual([fallback])
})

test("a dropped directory contributes the files inside it", async () => {
  const files = await filesFromDrop(
    transfer({
      entries: [directoryEntry([fileEntry("one.ojn"), fileEntry("two.ojm")])],
    })
  )
  expect(files.map((file) => file.name)).toEqual(["one.ojn", "two.ojm"])
})

test("a directory larger than one read batch is drained completely", async () => {
  // readEntries returns at most 100 entries per call and signals the end with
  // an empty batch. Reading once would silently drop everything past the first
  // hundred, which is the common case for a real song folder.
  const children = Array.from({ length: 250 }, (_, index) =>
    fileEntry(`song${index}.ojn`)
  )
  const files = await filesFromDrop(transfer({ entries: [directoryEntry(children)] }))
  expect(files).toHaveLength(250)
})

test("nested directories are walked to the bottom", async () => {
  const deep = directoryEntry([
    fileEntry("top.ojn"),
    directoryEntry([fileEntry("middle.ojm"), directoryEntry([fileEntry("deep.ojn")])]),
  ])
  const files = await filesFromDrop(transfer({ entries: [deep] }))
  expect(files.map((file) => file.name)).toEqual(["top.ojn", "middle.ojm", "deep.ojn"])
})

test("a file that cannot be read is skipped rather than failing the whole drop", async () => {
  const files = await filesFromDrop(
    transfer({
      entries: [fileEntry("good.ojn"), fileEntry("locked.ojn", { fails: true })],
    })
  )
  expect(files.map((file) => file.name)).toEqual(["good.ojn"])
})

test("an entry that is neither file nor directory is ignored", async () => {
  const files = await filesFromDrop(
    transfer({ entries: [{ isFile: false, isDirectory: false }, fileEntry("a.ojn")] })
  )
  expect(files.map((file) => file.name)).toEqual(["a.ojn"])
})

test("an empty drop yields no files", async () => {
  expect(await filesFromDrop(transfer({ entries: [] }))).toEqual([])
})
