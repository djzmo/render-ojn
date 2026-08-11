import * as React from "react"

import { dragHasFiles, filesFromDrop } from "@/lib/drop"

/**
 * Makes the whole window a drop target.
 *
 * The page already tells people to "drop files anywhere", so anywhere has to
 * mean it — dropping onto the header or an existing row must work exactly like
 * dropping onto the dashed box.
 *
 * Two details this has to get right:
 *
 * - dragenter/dragleave fire for every descendant the pointer crosses, so a
 *   naive boolean flickers as the cursor moves over rows. Counting enter/leave
 *   pairs and only clearing at zero keeps the state steady.
 * - The browser navigates to a file dropped outside a handler, discarding
 *   whatever is queued. preventDefault on window dragover suppresses that even
 *   when the drop lands somewhere with no listener of its own.
 */
export function usePageDrop(onFiles: (files: File[]) => void): boolean {
  const [isOver, setIsOver] = React.useState(false)
  const depth = React.useRef(0)
  // Kept in a ref so the listeners below never need re-binding. Written in an
  // effect rather than during render: a ref must not be mutated while
  // rendering, and the listeners only read it from event handlers anyway.
  const handler = React.useRef(onFiles)
  React.useEffect(() => {
    handler.current = onFiles
  }, [onFiles])

  React.useEffect(() => {
    const reset = () => {
      depth.current = 0
      setIsOver(false)
    }

    const onDragEnter = (event: DragEvent) => {
      if (!dragHasFiles(event.dataTransfer)) return
      event.preventDefault()
      depth.current += 1
      setIsOver(true)
    }

    const onDragOver = (event: DragEvent) => {
      if (!dragHasFiles(event.dataTransfer)) return
      // Required for the drop to fire at all, and what stops the browser from
      // navigating away to the dropped file.
      event.preventDefault()
      if (event.dataTransfer) event.dataTransfer.dropEffect = "copy"
    }

    const onDragLeave = (event: DragEvent) => {
      if (!dragHasFiles(event.dataTransfer)) return
      depth.current -= 1
      if (depth.current <= 0) reset()
    }

    const onDrop = async (event: DragEvent) => {
      if (!dragHasFiles(event.dataTransfer)) return
      event.preventDefault()
      reset()
      if (!event.dataTransfer) return
      const files = await filesFromDrop(event.dataTransfer)
      if (files.length) handler.current(files)
    }

    // A drag that leaves the window entirely fires no dragleave on some
    // platforms; dragend and blur are the backstops that clear the highlight.
    window.addEventListener("dragenter", onDragEnter)
    window.addEventListener("dragover", onDragOver)
    window.addEventListener("dragleave", onDragLeave)
    window.addEventListener("drop", onDrop)
    window.addEventListener("dragend", reset)
    window.addEventListener("blur", reset)

    return () => {
      window.removeEventListener("dragenter", onDragEnter)
      window.removeEventListener("dragover", onDragOver)
      window.removeEventListener("dragleave", onDragLeave)
      window.removeEventListener("drop", onDrop)
      window.removeEventListener("dragend", reset)
      window.removeEventListener("blur", reset)
    }
  }, [])

  return isOver
}
