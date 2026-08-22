[CmdletBinding(DefaultParameterSetName = "Run")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "Run")]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [Parameter(ParameterSetName = "Validate")]
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-AnimationSmokeResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration
    )

    $resultPath = Join-Path $Root "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Animation smoke did not write result.json: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ([int]$result.schema_version -ne 1) { $failures.Add("Result schema is not 1.") }
    if (-not [bool]$result.success) { $failures.Add("Packaged Animation runner reported failure.") }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        $failures.Add("Build configuration mismatch.")
    }
    if ([int]$result.animation_ir_major -ne 1 -or
        [int]$result.animation_ir_minor -ne 0 -or
        [string]$result.clock_domain -ne "Test") {
        $failures.Add("Versioned Animation IR or Virtual Clock evidence is invalid.")
    }
    if ([bool]$result.idle_ticker_registered -or
        [int64]$result.idle_ticker_invocations -ne 0) {
        $failures.Add("Idle Animation kernel owned work.")
    }
    if ([Math]::Abs([double]$result.quarter_value - 2.5) -gt 0.0001 -or
        [Math]::Abs([double]$result.retarget_start - 2.5) -gt 0.0001 -or
        [Math]::Abs([double]$result.retarget_midpoint - 11.25) -gt 0.0001 -or
        [Math]::Abs([double]$result.replace_start - 100.0) -gt 0.0001 -or
        [Math]::Abs([double]$result.cancel_effective - 77.0) -gt 0.0001 -or
        [Math]::Abs([double]$result.completion_exact_to - 1.0) -gt 0.0001) {
        $failures.Add("Virtual Clock samples or lease conflict outcomes are not exact.")
    }
    if (-not [bool]$result.completion_released -or
        -not [bool]$result.generation_released -or
        [string]$result.stale_cancel_result -ne "DroppedStaleGeneration" -or
        [int]$result.active_tracks_after_generation -ne 0 -or
        [bool]$result.ticker_after_generation -or
        -not [bool]$result.session_shutdown) {
        $failures.Add("Animation lease or Session lifecycle cleanup is incomplete.")
    }
    if ([int]$result.transaction_count -le 0 -or
        -not [bool]$result.all_transactions_committed -or
        [int]$result.trace_count -gt [int]$result.trace_budget) {
        $failures.Add("Transaction or bounded trace evidence is invalid.")
    }

    return [ordered]@{
        schema_version = 1
        success = ($failures.Count -eq 0)
        configuration = $ExpectedConfiguration
        generated_utc = [DateTime]::UtcNow.ToString("o")
        result = $resultPath
        animation_ir = "$($result.animation_ir_major).$($result.animation_ir_minor)"
        clock_domain = [string]$result.clock_domain
        idle_ticker_invocations = [int64]$result.idle_ticker_invocations
        quarter_value = [double]$result.quarter_value
        retarget_midpoint = [double]$result.retarget_midpoint
        cancel_effective = [double]$result.cancel_effective
        transaction_count = [int]$result.transaction_count
        trace_count = [int]$result.trace_count
        release_count = [int]$result.release_count
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Animation smoke executable must be a Win64 .exe: $resolvedExecutable"
        }
        if ([IO.Path]::GetFileName($resolvedExecutable) -eq "WebToUE.exe") {
            $stagedBinary = Join-Path (Split-Path -Parent $resolvedExecutable) `
                "WebToUE\Binaries\Win64\WebToUE-Win64-$Configuration.exe"
            if (Test-Path -LiteralPath $stagedBinary -PathType Leaf) {
                $resolvedExecutable = (Resolve-Path -LiteralPath $stagedBinary).Path
            }
        }
        if (Test-Path -LiteralPath (Join-Path $resolvedRoot "result.json")) {
            throw "Refusing to overwrite existing Animation evidence: $resolvedRoot"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        $logPath = Join-Path $resolvedRoot "WebToUE.log"
        $arguments = @(
            "-WTUEAnimationSmokeOutput=$resolvedRoot",
            "-abslog=$logPath",
            "-ResX=1280",
            "-ResY=720",
            "-windowed",
            "-NoSplash",
            "-NoVSync",
            "-unattended"
        )
        $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $arguments `
            -PassThru -Wait -WindowStyle Hidden
        if ($process.ExitCode -ne 0) {
            throw "Animation smoke process failed with exit code $($process.ExitCode)."
        }
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Animation smoke validation root does not exist: $resolvedRoot"
    }

    $summary = Test-AnimationSmokeResult -Root $resolvedRoot `
        -ExpectedConfiguration $Configuration
    $summaryPath = Join-Path $resolvedRoot "gate.json"
    $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $summary | ConvertTo-Json -Depth 10
    if (-not $summary.success) { exit 3 }
    exit 0
}
catch {
    [ordered]@{
        schema_version = 1
        success = $false
        configuration = $Configuration
        error = $_.Exception.Message
    } | ConvertTo-Json -Depth 5
    exit 2
}
