// @vitest-environment jsdom
import { act, renderHook } from "@testing-library/react"
import { expect, test, vi } from "vitest"

import { usePageDrop } from "@/hooks/use-page-drop"

/*
 * jsdom implements neither DataTransfer nor DragEvent's dataTransfer property,
 * so each event is a plain Event with the field assigned onto it -- which is
 * all the hook reads.
 */

function dragEvent(type: string, options: { types?: string[]; files?: File[] } = {}) {
  const event = new Event(type, { bubbles: true, cancelable: true })
  Object.defineProperty(event, "dataTransfer", {
    value: {
      types: options.types ?? ["Files"],
      items: (options.files ?? []).map((file) => ({
        webkitGetAsEntry: () => ({
          isFile: true,
          isDirectory: false,
          file: (onSuccess: (f: File) => void) => onSuccess(file),
        }),
      })),
      files: options.files ?? [],
      dropEffect: "none",
    },
  })
  return event
}

const fire = (event: Event) => act(() => void window.dispatchEvent(event))

test("dragging files over the window marks it as a drop target", () => {
  const view = renderHook(() => usePageDrop(vi.fn()))
  expect(view.result.current).toBe(false)

  fire(dragEvent("dragenter"))
  expect(view.result.current).toBe(true)
})

test("moving across nested elements does not flicker the highlight", () => {
  // dragenter and dragleave fire for every descendant the pointer crosses, so a
  // naive boolean turns the overlay on and off as the cursor moves over rows.
  const view = renderHook(() => usePageDrop(vi.fn()))

  fire(dragEvent("dragenter"))
  fire(dragEvent("dragenter"))
  fire(dragEvent("dragleave"))
  expect(view.result.current).toBe(true)

  fire(dragEvent("dragleave"))
  expect(view.result.current).toBe(false)
})

test("a drag carrying no files is ignored entirely", () => {
  const view = renderHook(() => usePageDrop(vi.fn()))
  fire(dragEvent("dragenter", { types: ["text/plain"] }))
  expect(view.result.current).toBe(false)
})

test("dragover is cancelled so the browser does not navigate to the file", () => {
  // Without preventDefault the browser opens the dropped file and discards
  // whatever is queued.
  renderHook(() => usePageDrop(vi.fn()))
  const event = dragEvent("dragover")
  fire(event)
  expect(event.defaultPrevented).toBe(true)
})

test("dropping files hands them to the callback and clears the highlight", async () => {
  const onFiles = vi.fn()
  const view = renderHook(() => usePageDrop(onFiles))
  const file = new File([new Uint8Array([1])], "song.ojn")

  fire(dragEvent("dragenter"))
  await act(async () => {
    window.dispatchEvent(dragEvent("drop", { files: [file] }))
    await Promise.resolve()
  })

  expect(onFiles).toHaveBeenCalledTimes(1)
  expect(onFiles.mock.calls[0][0].map((f: File) => f.name)).toEqual(["song.ojn"])
  expect(view.result.current).toBe(false)
})

test("a drop carrying nothing usable does not call the callback", async () => {
  const onFiles = vi.fn()
  renderHook(() => usePageDrop(onFiles))

  await act(async () => {
    window.dispatchEvent(dragEvent("drop", { files: [] }))
    await Promise.resolve()
  })
  expect(onFiles).not.toHaveBeenCalled()
})

test("a drag that ends outside the window clears a stuck highlight", () => {
  // Some platforms fire no dragleave when the pointer leaves the window, so
  // dragend and blur are the backstops.
  const view = renderHook(() => usePageDrop(vi.fn()))

  fire(dragEvent("dragenter"))
  fire(new Event("dragend"))
  expect(view.result.current).toBe(false)

  fire(dragEvent("dragenter"))
  fire(new Event("blur"))
  expect(view.result.current).toBe(false)
})

test("the latest callback is used without rebinding the listeners", async () => {
  const first = vi.fn()
  const second = vi.fn()
  const view = renderHook(({ handler }) => usePageDrop(handler), {
    initialProps: { handler: first },
  })

  view.rerender({ handler: second })
  await act(async () => {
    window.dispatchEvent(
      dragEvent("drop", { files: [new File([new Uint8Array([1])], "a.ojn")] })
    )
    await Promise.resolve()
  })

  expect(first).not.toHaveBeenCalled()
  expect(second).toHaveBeenCalledTimes(1)
})

test("listeners are removed when the component unmounts", () => {
  const onFiles = vi.fn()
  const view = renderHook(() => usePageDrop(onFiles))
  view.unmount()

  window.dispatchEvent(dragEvent("dragenter"))
  expect(onFiles).not.toHaveBeenCalled()
})
