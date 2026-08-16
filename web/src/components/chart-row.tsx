import * as React from "react"
import {
  AlertTriangleIcon,
  DownloadIcon,
  PlayIcon,
  XIcon,
} from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Progress } from "@/components/ui/progress"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { Skeleton } from "@/components/ui/skeleton"
import {
  downloadName,
  formatBytes,
  formatDuration,
  isReady,
  type ChartEntry,
} from "@/lib/pairing"
import {
  DEFAULT_DIFFICULTY,
  DIFFICULTY_NAMES,
  type Difficulty,
  type OutputFormat,
} from "@/lib/renderojn"
import { cn } from "@/lib/utils"

/** Two-digit slot number, the way a sampler labels its banks. */
function SlotNumber({ index, tone }: { index: number; tone: string }) {
  return (
    <div
      aria-hidden
      data-file-data
      className={cn(
        "flex w-8 shrink-0 justify-end pt-0.5 text-sm tabular-nums",
        tone
      )}
    >
      {String(index + 1).padStart(2, "0")}
    </div>
  )
}

interface ChartRowProps {
  entry: ChartEntry
  index: number
  format: OutputFormat
  onDifficultyChange: (id: string, difficulty: Difficulty) => void
  onRender: (id: string) => void
  onRemove: (id: string) => void
}

export function ChartRow({
  entry,
  index,
  format,
  onDifficultyChange,
  onRender,
  onRemove,
}: ChartRowProps) {
  const ready = isReady(entry)

  /*
   * The seat animation fires once, on the transition into paired — not on
   * every later re-render. Derived state during render is the supported way
   * to notice a prop change; a ref would be read during render, which is not.
   */
  const [seenReady, setSeenReady] = React.useState(ready)
  if (seenReady !== ready) {
    setSeenReady(ready)
  }
  const justPaired = ready && !seenReady

  const selected = entry.info?.difficulties.find(
    (d) => d.difficulty === entry.difficulty
  )

  if (entry.parseError) {
    return (
      <li className="flex items-start gap-3 px-4 py-3.5">
        <SlotNumber index={index} tone="text-destructive/70" />
        <div className="flex min-w-0 flex-1 flex-col gap-1">
          <div className="flex items-center gap-2">
            <AlertTriangleIcon className="size-3.5 shrink-0 text-destructive" />
            <span data-file-data className="truncate text-sm">
              {entry.fileName}
            </span>
          </div>
          <p className="text-sm text-muted-foreground">{entry.parseError}</p>
        </div>
        <RemoveButton onClick={() => onRemove(entry.id)} label={entry.fileName} />
      </li>
    )
  }

  if (!entry.info) {
    return (
      <li className="flex items-start gap-3 px-4 py-3.5">
        <SlotNumber index={index} tone="text-muted-foreground/50" />
        <div className="flex min-w-0 flex-1 flex-col gap-2">
          <Skeleton className="h-4 w-48" />
          <Skeleton className="h-3 w-32" />
        </div>
      </li>
    )
  }

  return (
    <li
      className={cn(
        "flex items-start gap-3 px-4 py-3.5",
        justPaired && "slot-paired"
      )}
    >
      <SlotNumber
        index={index}
        tone={ready ? "text-primary" : "text-destructive/70"}
      />

      <div className="flex min-w-0 flex-1 flex-col gap-2">
        <div className="flex flex-wrap items-baseline gap-x-2.5 gap-y-1">
          <h3 className="truncate text-sm font-medium">{entry.info.title}</h3>
          <span className="truncate text-sm text-muted-foreground">
            {entry.info.artist}
          </span>
        </div>

        {ready ? (
          <div className="flex flex-wrap items-center gap-x-3 gap-y-1 text-xs text-muted-foreground">
            <span data-file-data className="text-foreground/70">
              {entry.pkg.fileName}
            </span>
            {selected ? (
              <>
                <span aria-hidden>·</span>
                <span data-file-data>
                  {formatDuration(selected.durationSeconds)}
                </span>
              </>
            ) : null}
          </div>
        ) : (
          <MissingSlot packageName={entry.info.packageName} />
        )}

        {entry.render.status === "rendering" ? (
          <Progress
            value={Math.round(entry.render.progress * 100)}
            className="mt-1 gap-1.5"
            aria-label={`Rendering ${entry.info.title}`}
          />
        ) : null}

        {entry.render.status === "failed" ? (
          <p className="flex items-center gap-1.5 text-xs text-destructive">
            <AlertTriangleIcon className="size-3.5 shrink-0" />
            {entry.render.message}
          </p>
        ) : null}

        {entry.render.status === "done" && entry.warnings?.length
          ? entry.warnings.map((warning) => (
              <p
                key={warning}
                className="flex items-start gap-1.5 text-xs text-muted-foreground"
              >
                <AlertTriangleIcon className="mt-0.5 size-3.5 shrink-0" />
                {warning}
              </p>
            ))
          : null}
      </div>

      <div className="flex shrink-0 items-center gap-1.5">
        {ready ? (
          <>
            <Select
              value={entry.difficulty}
              onValueChange={(value) =>
                onDifficultyChange(entry.id, value as Difficulty)
              }
              // Only the difficulties this file actually contains.
              items={entry.info.difficulties.map((option) => ({
                value: option.difficulty,
                label: DIFFICULTY_NAMES[option.difficulty],
              }))}
              disabled={entry.render.status === "rendering"}
            >
              <SelectTrigger size="sm" aria-label="Difficulty">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  {entry.info.difficulties.map((option) => (
                    <SelectItem
                      key={option.difficulty}
                      value={option.difficulty}
                    >
                      {DIFFICULTY_NAMES[option.difficulty]}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>

            <RowAction
              entry={entry}
              format={format}
              onRender={() => onRender(entry.id)}
            />
          </>
        ) : (
          <Badge variant="outline" className="gap-1.5 text-muted-foreground">
            <span
              aria-hidden
              className="size-1.5 rounded-full bg-destructive"
            />
            Incomplete
          </Badge>
        )}

        <RemoveButton
          onClick={() => onRemove(entry.id)}
          label={entry.info.title}
        />
      </div>
    </li>
  )
}

/**
 * The signature element. An unpaired chart is a socket waiting for a
 * cartridge, named in the same monospace the file itself uses — not an error.
 */
function MissingSlot({ packageName }: { packageName: string }) {
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="relative flex h-5 items-center overflow-hidden rounded-sm border border-dashed border-destructive/45 px-2">
        <span aria-hidden className="slot-awaiting absolute inset-0" />
        <span className="relative text-muted-foreground">
          Needs{" "}
          <span data-file-data className="text-foreground">
            {packageName}
          </span>
        </span>
      </span>
    </div>
  )
}

function RowAction({
  entry,
  format,
  onRender,
}: {
  entry: ChartEntry
  format: OutputFormat
  onRender: () => void
}) {
  if (entry.render.status === "rendering") {
    return (
      <span
        data-file-data
        className="w-14 text-right text-xs tabular-nums text-primary"
      >
        {Math.round(entry.render.progress * 100)}%
      </span>
    )
  }

  if (entry.render.status === "done") {
    const name = downloadName(
      entry,
      DIFFICULTY_NAMES[entry.difficulty ?? DEFAULT_DIFFICULTY],
      entry.render.format,
      entry.render.tracks
    )
    return (
      <div className="flex items-center gap-1.5">
        <span
          data-file-data
          className="hidden text-xs text-muted-foreground tabular-nums sm:inline"
        >
          {formatBytes(entry.render.size)}
        </span>
        <Button size="sm" render={<a href={entry.render.url} download={name} />}>
          <DownloadIcon data-icon="inline-start" />
          {entry.render.format.toUpperCase()}
        </Button>
      </div>
    )
  }

  return (
    <Button size="sm" variant="outline" onClick={onRender}>
      <PlayIcon data-icon="inline-start" />
      Render {format.toUpperCase()}
    </Button>
  )
}

function RemoveButton({
  onClick,
  label,
}: {
  onClick: () => void
  label: string
}) {
  return (
    <Button
      size="icon-sm"
      variant="ghost"
      onClick={onClick}
      aria-label={`Remove ${label}`}
      className="text-muted-foreground"
    >
      <XIcon />
    </Button>
  )
}
