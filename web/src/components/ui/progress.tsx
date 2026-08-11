import { Progress as ProgressPrimitive } from "@base-ui/react/progress"

import { cn } from "@/lib/utils"

function Progress({
  className,
  children,
  value,
  ...props
}: ProgressPrimitive.Root.Props) {
  return (
    <ProgressPrimitive.Root
      value={value}
      data-slot="progress"
      className={cn("flex flex-wrap gap-3", className)}
      {...props}
    >
      {children}
      <ProgressTrack>
        <ProgressIndicator />
      </ProgressTrack>
    </ProgressPrimitive.Root>
  )
}

function ProgressTrack({ className, ...props }: ProgressPrimitive.Track.Props) {
  return (
    <ProgressPrimitive.Track
      /*
       * Deliberately `block`, not `flex`. The indicator is absolutely
       * positioned and sized by an inline percentage width; inside a flex
       * container that percentage resolves against a content box the single
       * absolutely-positioned child leaves at zero, so the bar renders 0px
       * wide however high the value climbs.
       */
      className={cn(
        "relative block h-1 w-full overflow-x-hidden rounded-full bg-muted",
        className
      )}
      data-slot="progress-track"
      {...props}
    />
  )
}

function ProgressIndicator({
  className,
  ...props
}: ProgressPrimitive.Indicator.Props) {
  return (
    <ProgressPrimitive.Indicator
      data-slot="progress-indicator"
      /*
       * Base UI sets the fill as an inline `width: N%` with
       * `inset-inline-start: 0`, and the track is the containing block.
       *
       * The width must be allowed to win. As a static flex item it collapses
       * to zero; positioned with both horizontal edges resolved, the trailing
       * edge wins instead and the bar renders 0px wide however high the
       * percentage climbs. Pinning only the leading edge and the top leaves
       * the inline width as the only thing sizing the element.
       */
      /*
       * Two local changes from upstream, both load-bearing:
       *
       * `inset-y-0` rather than a height utility, because Base UI sets
       * `height: inherit` inline -- that wins over any class and resolves
       * against the Root's `auto` height. Pinning top and bottom sizes the
       * fill from the track instead.
       *
       * No `transition-all`. The fill is an inline percentage width that the
       * renderer updates roughly every 100ms, which is faster than the 150ms
       * transition can settle; the animated value never reaches its target and
       * the bar renders 0px wide the whole way to 100%. Verified by cloning a
       * live indicator: the clone (no running animation) measured 201px where
       * the original measured 0px, from identical markup.
       */
      className={cn("absolute inset-y-0 left-0 bg-primary", className)}
      {...props}
    />
  )
}

function ProgressLabel({ className, ...props }: ProgressPrimitive.Label.Props) {
  return (
    <ProgressPrimitive.Label
      className={cn("text-sm font-medium", className)}
      data-slot="progress-label"
      {...props}
    />
  )
}

function ProgressValue({ className, ...props }: ProgressPrimitive.Value.Props) {
  return (
    <ProgressPrimitive.Value
      className={cn(
        "ml-auto text-sm text-muted-foreground tabular-nums",
        className
      )}
      data-slot="progress-value"
      {...props}
    />
  )
}

export {
  Progress,
  ProgressTrack,
  ProgressIndicator,
  ProgressLabel,
  ProgressValue,
}
