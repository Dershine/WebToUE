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

function Get-Counter {
    param(
        [Parameter(Mandatory = $true)]$Workload,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = "workload.$Name"
    if ($null -eq $Workload.PSObject.Properties[$property]) {
        throw "Transition smoke result is missing counter '$property'."
    }
    return [uint64]$Workload.$property
}

function Test-TransitionSmokeResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration
    )

    $resultPath = Join-Path $Root "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Transition smoke did not write result.json: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ([int]$result.schema_version -ne 6) { $failures.Add("Result schema is not 6.") }
    if (-not [bool]$result.success) { $failures.Add("Packaged runner reported failure.") }
    if ([string]$result.mode -ne "WebToUE" -or
        [string]$result.corpus -ne "TransitionSmoke") {
        $failures.Add("Packaged runner identity is not the Transition fixture.")
    }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        $failures.Add("Build configuration mismatch.")
    }
    if ([int]$result.compiled_document.compiled_resources -ne 0) {
        $failures.Add("Transition smoke unexpectedly compiled a Runtime resource.")
    }
    if (-not [bool]$result.product_policy.evaluated -or
        -not [bool]$result.product_policy.passed) {
        $failures.Add("Embedded Transition product policy failed.")
    }
    if (-not [bool]$result.screenshot_exists -or
        -not (Test-Path -LiteralPath ([string]$result.screenshot) -PathType Leaf)) {
        $failures.Add("Visible completed-hover screenshot evidence is missing.")
    }
    if ([int]$result.matching_renderer_frames -le 0 -or
        [double]$result.render_thread_ms.p95 -le 0.0 -or
        [double]$result.gpu_ms.p95 -le 0.0) {
        $failures.Add("Independent Renderer/RT/GPU evidence is missing.")
    }

    $transition = $result.transition
    if ($null -eq $transition -or
        [int]$transition.evidence_schema_version -ne 1 -or
        -not [bool]$transition.evaluated -or -not [bool]$transition.passed -or
        -not [bool]$transition.compiled_ir_valid) {
        $failures.Add("Versioned Transition Runtime evidence is incomplete.")
    }
    else {
        if ([int]$transition.compiled_transition_count -ne 5 -or
            [int]$transition.maximum_active_tracks -ne 5 -or
            [int]$transition.active_observation_frames -le 0) {
            $failures.Add("The legal five-address active Track set was not observed.")
        }
        if ([int]$transition.trace_count -gt [int]$transition.trace_budget -or
            [int]$transition.sampled_count -lt 5 -or
            [int]$transition.completed_count -lt 5 -or
            [int64]$transition.ticker_invocations -le 0 -or
            -not [bool]$transition.active_tracks_and_ticker_released) {
            $failures.Add("Bounded Clock/sample/completion or ticker release evidence is invalid.")
        }
        if ([int]$transition.transaction_count -le 0 -or
            [int]$transition.property_evaluation_count -le 0 -or
            [int]$transition.state_mutation_count -le 0 -or
            -not [bool]$transition.all_transactions_committed) {
            $failures.Add("Transition update transactions were not evaluated and committed.")
        }
        if (-not [bool]$transition.initial_semantic_target_found -or
            -not [bool]$transition.final_semantic_target_found -or
            -not [bool]$transition.semantic_bounds_changed) {
            $failures.Add("Completed visual-transform semantic bounds evidence is invalid.")
        }
        if ([uint64]$transition.display_commands_patched -le 0 -or
            [uint64]$transition.spatial_index_patches -le 0 -or
            [uint64]$transition.dirty_rects_added -le 0) {
            $failures.Add("Paint/display/spatial/dirty work was not observed.")
        }
        if ([uint64]$transition.yoga_style_writes -ne 0 -or
            [uint64]$transition.yoga_nodes_dirtied -ne 0 -or
            [uint64]$transition.yoga_results_changed -ne 0 -or
            [uint64]$transition.resource_load_attempts -ne 0) {
            $failures.Add("Paint-only Transition unexpectedly changed Yoga or loaded a resource.")
        }
    }

    foreach ($workload in @($result.setup_workload, $result.warmup_workload,
            $result.measurement_workload, $result.second_view_workload)) {
        if ((Get-Counter $workload "resource_load_attempts") -ne 0 -or
            (Get-Counter $workload "resource_failures") -ne 0) {
            $failures.Add("A synchronous resource load or failure was observed.")
            break
        }
    }

    return [ordered]@{
        schema_version = 1
        success = ($failures.Count -eq 0)
        configuration = $ExpectedConfiguration
        generated_utc = [DateTime]::UtcNow.ToString("o")
        result = $resultPath
        screenshot = [string]$result.screenshot
        transition_count = [int]$transition.compiled_transition_count
        maximum_active_tracks = [int]$transition.maximum_active_tracks
        active_observation_frames = [int]$transition.active_observation_frames
        sampled_count = [int]$transition.sampled_count
        completed_count = [int]$transition.completed_count
        ticker_invocations = [int64]$transition.ticker_invocations
        transaction_count = [int]$transition.transaction_count
        property_evaluation_count = [int]$transition.property_evaluation_count
        state_mutation_count = [int]$transition.state_mutation_count
        display_commands_patched = [uint64]$transition.display_commands_patched
        spatial_index_patches = [uint64]$transition.spatial_index_patches
        dirty_rects_added = [uint64]$transition.dirty_rects_added
        renderer_matching_frames = [int]$result.matching_renderer_frames
        game_thread_p95_ms = [double]$result.game_thread_ms.p95
        render_thread_p95_ms = [double]$result.render_thread_ms.p95
        gpu_p95_ms = [double]$result.gpu_ms.p95
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Transition smoke executable must be a Win64 .exe: $resolvedExecutable"
        }
        if ([IO.Path]::GetFileName($resolvedExecutable) -eq "WebToUE.exe") {
            $stagedBinary = Join-Path (Split-Path -Parent $resolvedExecutable) `
                "WebToUE\Binaries\Win64\WebToUE-Win64-$Configuration.exe"
            if (Test-Path -LiteralPath $stagedBinary -PathType Leaf) {
                $resolvedExecutable = (Resolve-Path -LiteralPath $stagedBinary).Path
            }
        }
        if (Test-Path -LiteralPath (Join-Path $resolvedRoot "result.json")) {
            throw "Refusing to overwrite existing Transition evidence: $resolvedRoot"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        $logPath = Join-Path $resolvedRoot "WebToUE.log"
        $arguments = @(
            "-WTUEBenchmark=WebToUE",
            "-WTUECorpus=TransitionSmoke",
            "-WTUEWarmupFrames=120",
            "-WTUESamples=240",
            "-WTUEOutput=$resolvedRoot",
            "-abslog=$logPath",
            "-ResX=1920",
            "-ResY=1080",
            "-windowed",
            "-NoSplash",
            "-NoVSync",
            "-unattended"
        )
        if ($Configuration -eq "Development") { $arguments += "-LLM" }
        $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $arguments `
            -PassThru -Wait -WindowStyle Hidden
        if ($process.ExitCode -ne 0) {
            throw "Transition smoke process failed with exit code $($process.ExitCode)."
        }
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Transition smoke validation root does not exist: $resolvedRoot"
    }

    $summary = Test-TransitionSmokeResult -Root $resolvedRoot `
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
