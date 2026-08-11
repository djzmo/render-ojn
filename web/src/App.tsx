import { FolderOpenIcon } from "lucide-react"

import { RenderQueue } from "@/components/render-queue"
import { Toaster } from "@/components/ui/sonner"
import { usePageDrop } from "@/hooks/use-page-drop"
import { useQueue } from "@/hooks/use-queue"

export default function App() {
  const queue = useQueue()
  // The whole window accepts a drop, not just the dashed box.
  const isDragging = usePageDrop(queue.addFiles)

  return (
    <div className="min-h-svh" data-dragging={isDragging || undefined}>
      <div className="mx-auto flex w-full max-w-4xl flex-col gap-8 px-4 py-10 sm:px-6 sm:py-14">
        <header className="flex flex-col gap-2">
          <h1 className="text-2xl font-semibold tracking-tight">RenderOJN</h1>
          <p className="max-w-prose text-sm text-muted-foreground">
            Turn O2Jam chart files into audio you can play. Everything runs in
            this tab. No file leaves your machine.
          </p>
        </header>

        <main>
          <RenderQueue
            charts={queue.charts}
            held={queue.held}
            format={queue.format}
            onFormatChange={queue.setFormat}
            quality={queue.quality}
            onQualityChange={queue.setQuality}
            rejected={queue.rejected}
            onDismissRejected={queue.dismissRejected}
            onFiles={queue.addFiles}
            onDifficultyChange={queue.setDifficulty}
            onRender={queue.renderOne}
            onRenderAll={queue.renderAll}
            onRemove={queue.removeChart}
            onClear={queue.clearAll}
            onDownloadAll={queue.downloadAll}
            isPacking={queue.isPacking}
            readyCount={queue.readyCount}
            doneCount={queue.doneCount}
            isRendering={queue.isRendering}
          />
        </main>

        <footer className="flex flex-wrap items-center justify-between gap-3 text-xs text-muted-foreground">
          <p>
            A chart names the sample package it needs. Drop both and they pair
            up on their own.
          </p>
          {/* -my-2/py-2 lifts the tap target to 44px without growing the footer. */}
          <a
            href="https://github.com/djzmo/render-ojn"
            target="_blank"
            rel="noreferrer"
            className="-my-3.5 inline-flex shrink-0 items-center gap-1.5 rounded-sm py-3.5 underline-offset-4 hover:text-foreground hover:underline focus-visible:ring-[3px] focus-visible:ring-ring/50 focus-visible:outline-none"
          >
            {/* Lucide dropped brand marks in v1; the real glyph, inlined. */}
            <svg
              viewBox="0 0 16 16"
              className="size-3.5 fill-current"
              aria-hidden
            >
              <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8Z" />
            </svg>
            GitHub
          </a>
        </footer>
      </div>

      {/*
       * Shown only while files are over the window. pointer-events-none is
       * essential: the overlay must not become the drop target itself, or it
       * would swallow the drop it is advertising.
       */}
      {isDragging ? (
        <div
          aria-hidden
          className="pointer-events-none fixed inset-0 z-50 flex items-center justify-center bg-background/80 p-6 backdrop-blur-sm"
        >
          <div className="flex flex-col items-center gap-3 rounded-xl border-2 border-dashed border-primary/60 px-10 py-12 text-center">
            <FolderOpenIcon className="size-8 text-primary" />
            <p className="text-lg font-medium">Drop to add</p>
            <p className="max-w-xs text-sm text-muted-foreground">
              Charts and sample packages, anywhere on the page.
            </p>
          </div>
        </div>
      ) : null}

      <Toaster />
    </div>
  )
}
