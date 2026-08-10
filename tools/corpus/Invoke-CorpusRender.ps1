<#
.SYNOPSIS
    Private corpus render harness for RenderOJN 1.0.0 verification.

.DESCRIPTION
    Renders every ordinary OJN in the supplied roots once, measures the
    resulting audio, and deletes the scratch WAV as soon as it is measured.
    Korea-era 'new' wrappers are recorded as expected skips rather than
    failures.

    This is a private verification tool. It never modifies, copies, or packages
    any source asset, and it never retains bulk audio.

.EXAMPLE
    ./Invoke-CorpusRender.ps1 -Root 'D:/O2Jam/Music' -ReportDirectory 'out/private-validation/corpus'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]] $Root,

    [Parameter(Mandatory = $true)]
    [string] $ReportDirectory,

    [string] $Executable = 'out/build/windows-x64/Release/RenderOJN.exe',

    [ValidateSet('e', 'n', 'h')]
    [string] $Difficulty = 'h',

    [ValidateSet('quick', 'realtime')]
    [string] $RenderMode = 'quick',

    # Bulk output is measured and deleted. Only these case ids are retained for
    # manual audition.
    [string[]] $AuditionWhitelist = @(),

    # Resume skips cases already present in the report.
    [switch] $Resume
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "RenderOJN executable not found: $Executable"
}

$null = New-Item -ItemType Directory -Path $ReportDirectory -Force
$scratchDirectory = Join-Path $ReportDirectory 'corpus-scratch'
$null = New-Item -ItemType Directory -Path $scratchDirectory -Force
$reportPath = Join-Path $ReportDirectory 'corpus-render.jsonl'
$summaryPath = Join-Path $ReportDirectory 'corpus-render-summary.json'

$completed = [System.Collections.Generic.HashSet[string]]::new()
if ($Resume -and (Test-Path -LiteralPath $reportPath)) {
    # A run killed mid-write leaves a truncated final line.  Skipping it is right
    # -- that case simply re-renders -- but silence would also hide a systemically
    # damaged report, so count and surface anything unparseable.
    $unreadable = 0
    foreach ($line in Get-Content -LiteralPath $reportPath) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        try { $null = $completed.Add((ConvertFrom-Json $line).case) } catch { $unreadable++ }
    }
    Write-Host "Resuming; $($completed.Count) case(s) already recorded."
    if ($unreadable -gt 0) {
        Write-Warning "$unreadable line(s) in $reportPath could not be parsed and were ignored; those cases will be re-rendered."
    }
}

# Reads the WAV header and measures the PCM payload without loading the whole
# file into memory as objects.
function Measure-Wave {
    param([string] $Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'RIFF') { throw 'not a RIFF file' }
        $null = $reader.ReadUInt32()
        if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'WAVE') { throw 'not a WAVE file' }

        $channels = 0; $rate = 0; $bits = 0; $dataSize = 0; $dataOffset = 0
        while ($stream.Position -lt $stream.Length - 8) {
            $id = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
            $size = $reader.ReadUInt32()
            if ($id -eq 'fmt ') {
                $null = $reader.ReadUInt16()
                $channels = $reader.ReadUInt16()
                $rate = $reader.ReadUInt32()
                $null = $reader.ReadUInt32()
                $null = $reader.ReadUInt16()
                $bits = $reader.ReadUInt16()
                if ($size -gt 16) { $null = $reader.ReadBytes([int]($size - 16)) }
            } elseif ($id -eq 'data') {
                $dataSize = $size
                $dataOffset = $stream.Position
                break
            } else {
                $null = $reader.ReadBytes([int]$size)
            }
        }
        if ($dataSize -eq 0) { throw 'no data chunk' }

        $stream.Position = $dataOffset
        $bytesPerFrame = [int]($channels * ($bits / 8))
        $frames = [int64]($dataSize / $bytesPerFrame)
        $peak = 0L
        $sumSquares = 0.0
        $sampleCount = 0L
        $buffer = New-Object byte[] 262144
        $remaining = [int64]$dataSize
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            while ($remaining -gt 0) {
                $read = $stream.Read($buffer, 0, [int][Math]::Min([int64]$buffer.Length, $remaining))
                if ($read -le 0) { break }
                $null = $sha.TransformBlock($buffer, 0, $read, $null, 0)
                for ($index = 0; $index + 1 -lt $read; $index += 2) {
                    $value = [BitConverter]::ToInt16($buffer, $index)
                    $magnitude = [Math]::Abs([int]$value)
                    if ($magnitude -gt $peak) { $peak = $magnitude }
                    $sumSquares += [double]$value * [double]$value
                    $sampleCount++
                }
                $remaining -= $read
            }
            $null = $sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
            $hash = ($sha.Hash | ForEach-Object { $_.ToString('x2') }) -join ''
        } finally {
            $sha.Dispose()
        }

        $rms = if ($sampleCount -gt 0) { [Math]::Sqrt($sumSquares / $sampleCount) / 32768.0 } else { 0.0 }
        [pscustomobject]@{
            channels   = $channels
            rate       = $rate
            bits       = $bits
            frames     = $frames
            peak       = [Math]::Round($peak / 32768.0, 6)
            rms        = [Math]::Round($rms, 6)
            nonSilent  = ($peak -gt 0)
            pcmSha256  = $hash
        }
    } finally {
        $stream.Dispose()
    }
}

$charts = [System.Collections.Generic.List[object]]::new()
$rootIndex = 0
foreach ($item in $Root) {
    $resolved = (Resolve-Path -LiteralPath $item).Path
    foreach ($file in Get-ChildItem -LiteralPath $resolved -Recurse -Filter *.ojn -File) {
        # The root-relative path alone is NOT a unique case id: two roots
        # routinely hold the same filenames (99 of 100 O2Jam Thai charts share a
        # name with an O2Jam chart), so the second root's results would overwrite
        # the first's here and in -Resume's already-done check.  The root's leaf
        # directory name is not sufficient either, because it can repeat across
        # roots (both `e-Games\O2Jam` and `NOWCOM\O2Jam` have the leaf `O2Jam`).
        # The root index is what makes the id unique whatever the naming.
        $relative = $file.FullName.Substring($resolved.Length).TrimStart('\', '/') -replace '\\', '/'
        $id = "$rootIndex#$relative"
        $charts.Add([pscustomobject]@{ Id = $id; Path = $file.FullName })
    }
    $rootIndex++
}
Write-Host "Discovered $($charts.Count) chart(s)."

# One scratch WAV exists at a time, so free space only needs to cover a single
# render plus headroom.
$largest = ($charts | ForEach-Object { (Get-Item -LiteralPath $_.Path).Length } | Measure-Object -Maximum).Maximum
$predicted = [int64]($largest * 200)
$drive = Get-PSDrive -Name ((Resolve-Path -LiteralPath $ReportDirectory).Path.Substring(0, 1))
if ($drive.Free -lt ($predicted * 2)) {
    Write-Warning "Free space $($drive.Free) may be below twice the predicted scratch size $predicted."
}

$totals = [ordered]@{ rendered = 0; skippedNew = 0; failed = 0; alreadyDone = 0; silent = 0 }

foreach ($chart in $charts) {
    if ($completed.Contains($chart.Id)) { $totals.alreadyDone++; continue }

    $scratch = Join-Path $scratchDirectory (([System.IO.Path]::GetFileNameWithoutExtension($chart.Path)) + '-' + [Guid]::NewGuid().ToString('N') + '.wav')
    # Redirecting a native executable's stderr inline wraps each line in an
    # ErrorRecord under Windows PowerShell 5.1, so capture both streams to files
    # and read them back as plain text.
    $stdoutPath = Join-Path $scratchDirectory 'stdout.txt'
    $stderrPath = Join-Path $scratchDirectory 'stderr.txt'
    $process = Start-Process -FilePath $Executable -NoNewWindow -Wait -PassThru `
        -ArgumentList @($chart.Path, '--difficulty', $Difficulty, '--rendermode', $RenderMode, '--format', 'wav', '--outfile', $scratch) `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $exit = $process.ExitCode
    $text = ((Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue) + "`n" +
             (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue))
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $record = [ordered]@{ case = $chart.Id; exitCode = $exit }
    if ($text -match "Korea-era 'new' wrappers") {
        $record.category = 'skipped_new'
        $totals.skippedNew++
    } elseif ($exit -ne 0 -or -not (Test-Path -LiteralPath $scratch)) {
        $record.category = 'failed'
        $record.output = $text.Trim()
        $totals.failed++
    } else {
        try {
            $measurement = Measure-Wave -Path $scratch
            $record.category = 'rendered'
            $record.measurement = $measurement
            if (-not $measurement.nonSilent) { $totals.silent++ }
            $totals.rendered++
        } catch {
            $record.category = 'failed'
            $record.output = $_.Exception.Message
            $totals.failed++
        }
    }

    $warnings = ($text -split "`n" | Where-Object { $_ -match '^warning:' } | ForEach-Object { $_.Trim() })
    if ($warnings) { $record.warnings = @($warnings) }

    ($record | ConvertTo-Json -Depth 6 -Compress) | Add-Content -LiteralPath $reportPath

    # Bulk audio is never retained unless the case was explicitly whitelisted.
    if (Test-Path -LiteralPath $scratch) {
        if ($AuditionWhitelist -contains $chart.Id) {
            Move-Item -LiteralPath $scratch -Destination (Join-Path $ReportDirectory ([System.IO.Path]::GetFileName($scratch))) -Force
        } else {
            Remove-Item -LiteralPath $scratch -Force
        }
    }
}

($totals | ConvertTo-Json -Depth 3) | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host "rendered=$($totals.rendered) skippedNew=$($totals.skippedNew) failed=$($totals.failed) silent=$($totals.silent) alreadyDone=$($totals.alreadyDone)"
if ($totals.failed -gt 0) { exit 1 }
