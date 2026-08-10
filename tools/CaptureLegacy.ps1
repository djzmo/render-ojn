[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $LegacyRoot,
    [Parameter(Mandatory = $true)] [string] $InputOjn,
    [Parameter(Mandatory = $true)] [string] $CaptureRoot,
    [ValidateSet('wav', 'mp3', 'ogg')] [string] $Format = 'wav',
    [ValidateSet('e', 'n', 'h')] [string] $Difficulty = 'h',
    [ValidateSet('quick', 'realtime')] [string] $RenderMode = 'quick'
)

$ErrorActionPreference = 'Stop'
$legacy = (Resolve-Path -LiteralPath $LegacyRoot).Path
$input = (Resolve-Path -LiteralPath $InputOjn).Path
$capture = [IO.Path]::GetFullPath($CaptureRoot)

$header = [IO.File]::ReadAllBytes($input)
if ($header.Length -lt 268) {
    throw "Input OJN is too small to contain a sample-package name."
}
$packageName = [Text.Encoding]::GetEncoding(28591).GetString($header, 236, 32).TrimEnd([char]0)
if ([string]::IsNullOrWhiteSpace($packageName) -or [IO.Path]::GetFileName($packageName) -ne $packageName) {
    throw "Input OJN has an unsafe or missing sample-package name."
}
$package = Join-Path ([IO.Path]::GetDirectoryName($input)) $packageName
if (-not (Test-Path -LiteralPath $package -PathType Leaf)) {
    throw "Unable to find the OJN header-named sample package beside the input: $packageName"
}

$required = @('RenderOJN.exe', 'fmodex.dll', 'libmp3lame.dll', 'libsndfile-1.dll')
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $legacy $name) -PathType Leaf)) {
        throw "Missing required legacy runtime input: $name"
    }
}

New-Item -ItemType Directory -Force -Path $capture | Out-Null
$temporary = Join-Path ([IO.Path]::GetTempPath()) ("renderojn-legacy-capture-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $temporary | Out-Null
try {
    # The original executable and its DLLs remain read-only inputs.  Only copies in a disposable directory are executed.
    foreach ($name in $required) { Copy-Item -LiteralPath (Join-Path $legacy $name) -Destination $temporary -ErrorAction Stop }
    Copy-Item -LiteralPath $input -Destination (Join-Path $temporary ([IO.Path]::GetFileName($input))) -ErrorAction Stop
    Copy-Item -LiteralPath $package -Destination (Join-Path $temporary $packageName) -ErrorAction Stop
    $stdout = Join-Path $capture 'legacy.stdout.txt'
    $stderr = Join-Path $capture 'legacy.stderr.txt'
    $outBase = Join-Path $capture 'legacy-render'
    $stagedInput = Join-Path $temporary ([IO.Path]::GetFileName($input))
    $arguments = @($stagedInput, '--outfile', $outBase, '--format', $Format, '--difficulty', $Difficulty, '--rendermode', $RenderMode)
    $process = Start-Process -FilePath (Join-Path $temporary 'RenderOJN.exe') -ArgumentList $arguments -WorkingDirectory $temporary `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -Wait -PassThru
    $output = "$outBase.$Format"
    $metadata = [ordered]@{
        command = @('RenderOJN.exe') + $arguments
        exit_status = $process.ExitCode
        stdout = [IO.Path]::GetFileName($stdout)
        stderr = [IO.Path]::GetFileName($stderr)
        input_sha256 = (Get-FileHash -LiteralPath $input -Algorithm SHA256).Hash
        output = if (Test-Path -LiteralPath $output) {
            [ordered]@{ path = [IO.Path]::GetFileName($output); sha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash; bytes = (Get-Item -LiteralPath $output).Length }
        } else { $null }
        decoded_audio = $null
        tags = $null
    }
    $ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
    if ($ffprobe -and (Test-Path -LiteralPath $output)) {
        $probe = & $ffprobe.Source -v error -show_entries 'format_tags:stream=codec_name,channels,sample_rate,duration' -of json -- $output | ConvertFrom-Json
        $metadata.decoded_audio = $probe.streams
        $metadata.tags = $probe.format.tags
    }
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $capture 'legacy-capture.json') -Encoding utf8
    Write-Host "Legacy capture recorded in $capture"
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
