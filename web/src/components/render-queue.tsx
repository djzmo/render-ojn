import { DownloadIcon, PlayIcon, Trash2Icon } from "lucide-react"

import { ChartRow } from "@/components/chart-row"
import { Dropzone } from "@/components/dropzone"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"
import { Spinner } from "@/components/ui/spinner"
import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group"
import { isReady, type ChartEntry, type SamplePackage } from "@/lib/pairing"
import {
  OUTPUT_FORMATS,
  QUALITY_DETAIL,
  QUALITY_LABELS,
  QUALITY_VALUES,
  RENDER_MODE_DETAIL,
  RENDER_MODE_LABELS,
  RENDER_MODES,
  TRACKS_DETAIL,
  TRACKS_LABELS,
  TRACKS_OPTIONS,
  type Difficulty,
  type OutputFormat,
  type Quality,
  type RenderMode,
  type Tracks,
} from "@/lib/renderojn"

interface RenderQueueProps {
  charts: ChartEntry[]
  held: SamplePackage[]
  format: OutputFormat
  onFormatChange: (format: OutputFormat) => void
  quality: Quality
  onQualityChange: (quality: Quality) => void
  tracks: Tracks
  onTracksChange: (tracks: Tracks) => void
  renderMode: RenderMode
  onRenderModeChange: (renderMode: RenderMode) => void
  rejected: string[]
  onDismissRejected: () => void
  onFiles: (files: File[]) => void
  onDifficultyChange: (id: string, difficulty: Difficulty) => void
  onRender: (id: string) => void
  onRenderAll: () => void
  onRemove: (id: string) => void
  onClear: () => void
  onDownloadAll: () => void
  isPacking: boolean
  readyCount: number
  doneCount: number
  isRendering: boolean
}

export function RenderQueue({
  charts,
  held,
  format,
  onFormatChange,
  quality,
  onQualityChange,
  tracks,
  onTracksChange,
  renderMode,
  onRenderModeChange,
  rejected,
  onDismissRejected,
  onFiles,
  onDifficultyChange,
  onRender,
  onRenderAll,
  onRemove,
  onClear,
  onDownloadAll,
  isPacking,
  readyCount,
  doneCount,
  isRendering,
}: RenderQueueProps) {
  const incompleteCount = charts.filter(
    (chart) => chart.info && !isReady(chart)
  ).length
  const pendingCount = charts.filter(
    (chart) => isReady(chart) && chart.render.status !== "done"
  ).length

  if (!charts.length) {
    return (
      <div className="flex flex-col gap-4">
        <Dropzone onFiles={onFiles} />
        {held.length ? <HeldNotice held={held} /> : null}
        {rejected.length ? (
          <RejectedNotice files={rejected} onDismiss={onDismissRejected} />
        ) : null}
      </div>
    )
  }

  return (
    <div className="flex flex-col gap-4">
      {rejected.length ? (
        <RejectedNotice files={rejected} onDismiss={onDismissRejected} />
      ) : null}

      <Card className="gap-0 overflow-hidden py-0">
        <CardHeader className="gap-3 border-b py-4 sm:flex-row sm:items-center sm:justify-between">
          <div className="flex flex-col gap-1">
            <CardTitle className="text-base">Queue</CardTitle>
            <CardDescription>
              {charts.length} {charts.length === 1 ? "chart" : "charts"}
              {incompleteCount
                ? ` · ${incompleteCount} waiting on a package`
                : null}
              {doneCount ? ` · ${doneCount} rendered` : null}
              {format === "wav" ? " · WAV is uncompressed" : null}
            </CardDescription>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <ToggleGroup
              value={[format]}
              onValueChange={(value) => {
                // Base UI reports an array; ignore the deselect-to-empty case.
                if (value.length) onFormatChange(value[0] as OutputFormat)
              }}
              variant="outline"
              size="sm"
              spacing={0}
              aria-label="Output format"
            >
              {OUTPUT_FORMATS.map((option) => (
                <ToggleGroupItem
                  key={option}
                  value={option}
                  aria-label={option.toUpperCase()}
                >
                  {option.toUpperCase()}
                </ToggleGroupItem>
              ))}
            </ToggleGroup>

            <Separator orientation="vertical" className="hidden h-6 sm:block" />

            {/*
             * WAV is uncompressed 16-bit stereo, so the bitrate control does
             * nothing there — the CLI says as much ("WAV ignores --quality").
             * Disabling it is clearer than leaving a live-looking control that
             * cannot change the output.
             */}
            <ToggleGroup
              value={[String(quality)]}
              onValueChange={(value) => {
                if (value.length) onQualityChange(Number(value[0]) as Quality)
              }}
              variant="outline"
              size="sm"
              spacing={0}
              disabled={format === "wav"}
              aria-label="Bitrate"
              aria-describedby="bitrate-note"
            >
              {QUALITY_VALUES.map((option) => (
                <ToggleGroupItem
                  key={option}
                  value={String(option)}
                  title={QUALITY_DETAIL[option]}
                >
                  {QUALITY_LABELS[option]}
                </ToggleGroupItem>
              ))}
            </ToggleGroup>

            <span id="bitrate-note" className="sr-only">
              {format === "wav"
                ? "Bitrate does not apply to WAV, which is uncompressed."
                : QUALITY_DETAIL[quality]}
            </span>

            <Separator orientation="vertical" className="hidden h-6 sm:block" />

            {/*
             * Which note roles to sound. Keysounds are the playable lanes; BGM
             * is the autoplay/background stream. Every mode is the same length —
             * unselected notes are muted, not removed. Background fidelity is
             * chart-dependent (samples are shared between roles), so the tooltip
             * says so rather than promising a clean instrumental.
             */}
            <ToggleGroup
              value={[tracks]}
              onValueChange={(value) => {
                if (value.length) onTracksChange(value[0] as Tracks)
              }}
              variant="outline"
              size="sm"
              spacing={0}
              aria-label="Tracks"
              aria-describedby="tracks-note"
            >
              {TRACKS_OPTIONS.map((option) => (
                <ToggleGroupItem
                  key={option}
                  value={option}
                  title={TRACKS_DETAIL[option]}
                >
                  {TRACKS_LABELS[option]}
                </ToggleGroupItem>
              ))}
            </ToggleGroup>

            <span id="tracks-note" className="sr-only">
              {TRACKS_DETAIL[tracks]}
            </span>

            <Separator orientation="vertical" className="hidden h-6 sm:block" />

            {/*
             * Scheduling mode, mirroring the CLI's --rendermode. Quick places
             * each trigger at its exact frame; Realtime uses the 48-frame
             * scheduling the game engine drove audio with. The browser never
             * wall-clock-paces, so this only changes the scheduling.
             */}
            <ToggleGroup
              value={[renderMode]}
              onValueChange={(value) => {
                if (value.length) onRenderModeChange(value[0] as RenderMode)
              }}
              variant="outline"
              size="sm"
              spacing={0}
              aria-label="Scheduling mode"
              aria-describedby="rendermode-note"
            >
              {RENDER_MODES.map((option) => (
                <ToggleGroupItem
                  key={option}
                  value={option}
                  title={RENDER_MODE_DETAIL[option]}
                >
                  {RENDER_MODE_LABELS[option]}
                </ToggleGroupItem>
              ))}
            </ToggleGroup>

            <span id="rendermode-note" className="sr-only">
              {RENDER_MODE_DETAIL[renderMode]}
            </span>
          </div>
        </CardHeader>

        <CardContent className="px-0">
          <ul className="flex flex-col divide-y divide-border">
            {charts.map((entry, index) => (
              <ChartRow
                key={entry.id}
                entry={entry}
                index={index}
                format={format}
                onDifficultyChange={onDifficultyChange}
                onRender={onRender}
                onRemove={onRemove}
              />
            ))}
          </ul>
        </CardContent>

        <CardFooter className="flex-wrap justify-between gap-3 border-t py-3.5">
          <div className="flex items-center gap-2 text-sm text-muted-foreground">
            {readyCount ? (
              <Badge variant="secondary" data-file-data>
                {readyCount} ready
              </Badge>
            ) : (
              <span>Nothing is ready to render yet</span>
            )}
          </div>
          <div className="flex flex-wrap gap-2">
            <Button variant="ghost" size="sm" onClick={onClear}>
              <Trash2Icon data-icon="inline-start" />
              Clear queue
            </Button>
            {/*
             * Only worth offering for more than one file: a single render
             * already has its own download, and wrapping it in a zip would add
             * a step rather than remove one.
             */}
            {doneCount > 1 ? (
              <Button
                variant="outline"
                size="sm"
                onClick={onDownloadAll}
                disabled={isPacking}
              >
                {isPacking ? (
                  <Spinner data-icon="inline-start" />
                ) : (
                  <DownloadIcon data-icon="inline-start" />
                )}
                {isPacking ? "Packing" : `Download all (${doneCount})`}
              </Button>
            ) : null}
            <Button
              size="sm"
              onClick={onRenderAll}
              disabled={!pendingCount || isRendering}
            >
              <PlayIcon data-icon="inline-start" />
              {isRendering
                ? "Rendering"
                : pendingCount === 1
                  ? "Render 1 song"
                  : `Render ${pendingCount} songs`}
            </Button>
          </div>
        </CardFooter>
      </Card>

      <Dropzone onFiles={onFiles} compact />
      {held.length ? <HeldNotice held={held} /> : null}
    </div>
  )
}

/** Packages nothing has claimed. Held quietly, per §4 — not an error. */
function HeldNotice({ held }: { held: SamplePackage[] }) {
  return (
    <div className="flex flex-wrap items-center gap-x-2 gap-y-1 px-1 text-xs text-muted-foreground">
      <span>
        {held.length} unmatched {held.length === 1 ? "package" : "packages"} held
        for later:
      </span>
      {held.slice(0, 4).map((pkg) => (
        <span key={pkg.id} data-file-data className="text-foreground/60">
          {pkg.fileName}
        </span>
      ))}
      {held.length > 4 ? <span>and {held.length - 4} more</span> : null}
    </div>
  )
}

function RejectedNotice({
  files,
  onDismiss,
}: {
  files: string[]
  onDismiss: () => void
}) {
  return (
    <Alert>
      <AlertTitle>
        {files.length} {files.length === 1 ? "file was" : "files were"} skipped
      </AlertTitle>
      <AlertDescription>
        <p>
          RenderOJN reads <span data-file-data>.ojn</span> charts and{" "}
          <span data-file-data>.ojm</span> packages. Skipped:{" "}
          <span data-file-data>{files.slice(0, 3).join(", ")}</span>
          {files.length > 3 ? ` and ${files.length - 3} more` : ""}.
        </p>
        <Button variant="outline" size="sm" onClick={onDismiss}>
          Dismiss
        </Button>
      </AlertDescription>
    </Alert>
  )
}
