/**
 * Reading files out of a drop.
 *
 * Shared by the dropzone and the page-level drop target so both accept the same
 * things — a folder dropped on the header must behave exactly like one dropped
 * on the dashed box.
 */

/** Walks a dropped directory tree; folders are the common case here. */
async function collectFromEntry(
  entry: FileSystemEntry,
  out: File[]
): Promise<void> {
  if (entry.isFile) {
    const file = await new Promise<File | null>((resolve) =>
      (entry as FileSystemFileEntry).file(resolve, () => resolve(null))
    )
    if (file) out.push(file)
    return
  }
  if (entry.isDirectory) {
    const reader = (entry as FileSystemDirectoryEntry).createReader()
    // readEntries returns at most 100 per call, so drain it.
    for (;;) {
      const batch = await new Promise<FileSystemEntry[]>((resolve) =>
        reader.readEntries(resolve, () => resolve([]))
      )
      if (!batch.length) break
      for (const child of batch) {
        await collectFromEntry(child, out)
      }
    }
  }
}

export async function filesFromDrop(dataTransfer: DataTransfer): Promise<File[]> {
  const entries = Array.from(dataTransfer.items)
    .map((item) => item.webkitGetAsEntry?.())
    .filter((entry): entry is FileSystemEntry => Boolean(entry))

  if (!entries.length) {
    return Array.from(dataTransfer.files)
  }

  const out: File[] = []
  for (const entry of entries) {
    await collectFromEntry(entry, out)
  }
  return out
}

/**
 * True when a drag carries files rather than, say, selected text.
 *
 * Dragging a text selection across the window should not light up the page as
 * though it were about to accept something.
 */
export function dragHasFiles(dataTransfer: DataTransfer | null): boolean {
  if (!dataTransfer) return false
  return Array.from(dataTransfer.types).includes("Files")
}
