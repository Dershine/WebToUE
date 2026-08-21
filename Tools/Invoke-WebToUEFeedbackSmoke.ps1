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

function Test-FeedbackSmokeResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration
    )

    $resultPath = Join-Path $Root "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Feedback smoke did not write result.json: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ([int]$result.schema_version -ne 1) { $failures.Add("Result schema is not 1.") }
    if (-not [bool]$result.success) { $failures.Add("Packaged Feedback runner reported failure.") }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        $failures.Add("Build configuration mismatch.")
    }
    if ([string]$result.profile_path -ne
        "/Game/WebToUEExamples/Audio/DA_WTUE_FeedbackProfile.DA_WTUE_FeedbackProfile" -or
        [string]$result.profile_id -ne "webtoue.feedback.default" -or
        [int]$result.profile_schema_major -ne 1 -or
        [int]$result.profile_schema_minor -ne 0 -or
        [int]$result.sealed_dependency_count -le 0) {
        $failures.Add("Versioned packaged Feedback Profile identity is invalid.")
    }
    if ([string]$result.resource_request_contract -ne "async-only" -or
        -not [bool]$result.critical_ready_observed -or
        -not [bool]$result.host_attached) {
        $failures.Add("Critical residency or Screen Host gate evidence is incomplete.")
    }
    if ([string]$result.confirm -ne "Routed" -or
        [string]$result.cooldown -ne "DroppedByRouter" -or
        [string]$result.hover -ne "Routed" -or
        [string]$result.deduplicated_focus -ne "DroppedByRouter" -or
        [string]$result.throttled_navigate -ne "DroppedByRouter" -or
        [string]$result.muted_cancel -ne "DroppedByRouter" -or
        [string]$result.missing_cue -ne "DroppedByRouter") {
        $failures.Add("Packaged Feedback policy outcomes are incomplete.")
    }
    if ([int]$result.requested_count -ne 11 -or
        [int]$result.committed_count -ne 11 -or
        [int]$result.routed_count -ne 6) {
        $failures.Add("Packaged Feedback trace counts are not deterministic.")
    }
    if ([int]$result.backend_attempt_count -ne 6 -or
        [int]$result.backend_success_count -ne 6 -or
        [string]$result.last_playback_mode -ne "Screen2D" -or
        -not [bool]$result.last_request_has_concurrency) {
        $failures.Add("Default UE backend invocation evidence is incomplete.")
    }

    return [ordered]@{
        schema_version = 1
        success = ($failures.Count -eq 0)
        configuration = $ExpectedConfiguration
        generated_utc = [DateTime]::UtcNow.ToString("o")
        result = $resultPath
        profile_path = [string]$result.profile_path
        sealed_dependency_count = [int]$result.sealed_dependency_count
        critical_pending_observed = [bool]$result.critical_pending_observed
        critical_ready_observed = [bool]$result.critical_ready_observed
        requested_count = [int]$result.requested_count
        committed_count = [int]$result.committed_count
        routed_count = [int]$result.routed_count
        backend_success_count = [int]$result.backend_success_count
        playback_mode = [string]$result.last_playback_mode
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Feedback smoke executable must be a Win64 .exe: $resolvedExecutable"
        }
        if ([IO.Path]::GetFileName($resolvedExecutable) -eq "WebToUE.exe") {
            $stagedBinary = Join-Path (Split-Path -Parent $resolvedExecutable) `
                "WebToUE\Binaries\Win64\WebToUE-Win64-$Configuration.exe"
            if (Test-Path -LiteralPath $stagedBinary -PathType Leaf) {
                $resolvedExecutable = (Resolve-Path -LiteralPath $stagedBinary).Path
            }
        }
        if (Test-Path -LiteralPath (Join-Path $resolvedRoot "result.json")) {
            throw "Refusing to overwrite existing Feedback evidence: $resolvedRoot"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        $logPath = Join-Path $resolvedRoot "WebToUE.log"
        $arguments = @(
            "-WTUEFeedbackSmokeOutput=$resolvedRoot",
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
            throw "Feedback smoke process failed with exit code $($process.ExitCode)."
        }
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Feedback smoke validation root does not exist: $resolvedRoot"
    }

    $summary = Test-FeedbackSmokeResult -Root $resolvedRoot `
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
