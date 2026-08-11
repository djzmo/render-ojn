import * as React from "react"
import { FolderOpenIcon } from "lucide-react"

import { Button } from "@/components/ui/button"
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty"
import { cn } from "@/lib/utils"

const ACCEPT = ".ojn,.ojm"

interface DropzoneProps {
  onFiles: (files: File[]) => void
  /** Compact form once the queue has rows. */
  compact?: boolean
}

/**
 * The file-picker affordance.
 *
 * Drops are handled once, at the window level (see `usePageDrop`), so this
 * component deliberately has no drop handler of its own — two handlers on
 * nested elements would ingest the same files twice.
 */
export function Dropzone({ onFiles, compact = false }: DropzoneProps) {
  const inputRef = React.useRef<HTMLInputElement>(null)
  const folderRef = React.useRef<HTMLInputElement>(null)

  const handlePick = React.useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      const files = Array.from(event.target.files ?? [])
      if (files.length) onFiles(files)
      // Reset so re-picking the same file fires change again.
      event.target.value = ""
    },
    [onFiles]
  )

  const openFiles = React.useCallback(() => inputRef.current?.click(), [])
  const openFolder = React.useCallback(() => folderRef.current?.click(), [])

  return (
    <div
      className={cn(
        "rounded-lg border border-dashed border-border transition-colors",
        compact && "border-border/70"
      )}
    >
      <input
        ref={inputRef}
        type="file"
        multiple
        accept={ACCEPT}
        onChange={handlePick}
        className="sr-only"
        tabIndex={-1}
      />
      {/* A separate input, because webkitdirectory forbids picking loose files. */}
      <input
        ref={folderRef}
        type="file"
        multiple
        onChange={handlePick}
        className="sr-only"
        tabIndex={-1}
        {...({ webkitdirectory: "" } as Record<string, string>)}
      />

      {compact ? (
        <div className="flex flex-wrap items-center justify-between gap-3 px-4 py-3">
          <p className="text-sm text-muted-foreground">
            Drop more files anywhere on the page
          </p>
          <div className="flex flex-wrap gap-2">
            <Button variant="outline" size="sm" onClick={openFolder}>
              <FolderOpenIcon data-icon="inline-start" />
              Add folder
            </Button>
            <Button variant="ghost" size="sm" onClick={openFiles}>
              Add files
            </Button>
          </div>
        </div>
      ) : (
        <Empty className="border-0 py-16">
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <FolderOpenIcon />
            </EmptyMedia>
            <EmptyTitle>Drop a song folder to begin</EmptyTitle>
            <EmptyDescription>
              Charts are <span data-file-data>.ojn</span> and their samples are{" "}
              <span data-file-data>.ojm</span>. Drop the whole folder and each
              chart picks up the package it names.
            </EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <div className="flex flex-wrap items-center justify-center gap-2">
              <Button onClick={openFolder}>
                <FolderOpenIcon data-icon="inline-start" />
                Choose folder
              </Button>
              <Button variant="outline" onClick={openFiles}>
                Choose files
              </Button>
            </div>
          </EmptyContent>
        </Empty>
      )}
    </div>
  )
}
