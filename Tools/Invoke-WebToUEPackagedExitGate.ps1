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
    [switch]$ValidateOnly,

    [ValidateRange(30, 100000)]
    [int]$Samples = 600,

    [ValidateRange(10, 10000)]
    [int]$WarmupFrames = 120,

    [ValidateRange(1, 9)]
    [int]$ColdTrials = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:GateSchemaVersion = 1
$script:ResultSchemaVersion = 6
$script:MaximumEndToEndRatio = 2.0
$script:MaximumDevelopmentLlmDeltaMiB = 64.0
$script:MaximumCompiledResources = 0
$script:ResolutionX = 1920
$script:ResolutionY = 1080
$script:BatchCaps = @{
    MainMenu = 12
    HUD = 6
    ScrollableSettings = 14
}
$script:VertexCaps = @{
    MainMenu = 492
    HUD = 136
    ScrollableSettings = 384
}
$script:Modes = @("WebToUE", "UMG")
$script:Corpora = @("MainMenu", "HUD", "ScrollableSettings")

function Get-PercentileValue {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Metric,
        [string]$Percentile = "p95"
    )

    $distribution = $Result.$Metric
    if ($null -eq $distribution) {
        throw "Result is missing metric '$Metric'."
    }
    return [double]$distribution.$Percentile
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)

    if ($Values.Count -eq 0) {
        throw "Cannot calculate a median for an empty sample."
    }
    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return [double]$sorted[$middle]
    }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Get-CaseDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$Corpus,
        [int]$Trial = 1
    )

    if ($Trial -eq 1) {
        return Join-Path (Join-Path $Root "Full") "$Mode-$Corpus"
    }
    return Join-Path (Join-Path (Join-Path $Root "Cold") ("Trial-{0:D2}" -f $Trial)) "$Mode-$Corpus"
}

function Read-BenchmarkResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$Corpus,
        [int]$Trial = 1
    )

    $caseDirectory = Get-CaseDirectory -Root $Root -Mode $Mode -Corpus $Corpus -Trial $Trial
    $resultPath = Join-Path $caseDirectory "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Missing benchmark result: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    return [pscustomobject]@{
        Directory = $caseDirectory
        Path = $resultPath
        Result = $result
    }
}

function Get-BenchmarkArguments {
    param(
        [Parameter(Mandatory = $true)][string]$CaseDirectory,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$Corpus,
        [Parameter(Mandatory = $true)][int]$CaseSamples,
        [Parameter(Mandatory = $true)][int]$CaseWarmupFrames,
        [Parameter(Mandatory = $true)][ValidateSet("Development", "Shipping")]
        [string]$CaseConfiguration
    )

    $logPath = Join-Path $CaseDirectory "WebToUE.log"
    $arguments = @(
        "-WTUEBenchmark=$Mode",
        "-WTUECorpus=$Corpus",
        "-WTUEWarmupFrames=$CaseWarmupFrames",
        "-WTUESamples=$CaseSamples",
        "-WTUEOutput=$CaseDirectory",
        "-abslog=$logPath",
        "-ResX=$($script:ResolutionX)",
        "-ResY=$($script:ResolutionY)",
        "-windowed",
        "-NoSplash",
        "-NoVSync",
        "-unattended"
    )
    if ($CaseConfiguration -eq "Development") {
        $arguments += "-LLM"
    }
    return $arguments
}

function Invoke-BenchmarkCase {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [Parameter(Mandatory = $true)][string]$CaseDirectory,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$Corpus,
        [Parameter(Mandatory = $true)][int]$CaseSamples,
        [Parameter(Mandatory = $true)][int]$CaseWarmupFrames,
        [Parameter(Mandatory = $true)][ValidateSet("Development", "Shipping")]
        [string]$CaseConfiguration
    )

    if (Test-Path -LiteralPath (Join-Path $CaseDirectory "result.json")) {
        throw "Refusing to overwrite an existing benchmark result: $CaseDirectory"
    }
    New-Item -ItemType Directory -Force -Path $CaseDirectory | Out-Null
    $arguments = @(Get-BenchmarkArguments -CaseDirectory $CaseDirectory -Mode $Mode `
        -Corpus $Corpus -CaseSamples $CaseSamples -CaseWarmupFrames $CaseWarmupFrames `
        -CaseConfiguration $CaseConfiguration)
    # A hidden Win32 game window does not preserve the real Slate pointer/present
    # contract: hover/scroll trajectories can miss and renderer batches can change.
    $process = Start-Process -FilePath $ExecutablePath -ArgumentList $arguments `
        -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        throw "Benchmark process failed with exit code $($process.ExitCode): $Mode/$Corpus"
    }
    $resultPath = Join-Path $CaseDirectory "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Benchmark process did not write result.json: $Mode/$Corpus"
    }
}

function Invoke-BenchmarkMatrix {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][int]$FullSamples,
        [Parameter(Mandatory = $true)][int]$FullWarmupFrames,
        [Parameter(Mandatory = $true)][int]$RequestedColdTrials,
        [Parameter(Mandatory = $true)][ValidateSet("Development", "Shipping")]
        [string]$MatrixConfiguration
    )

    foreach ($mode in $script:Modes) {
        foreach ($corpus in $script:Corpora) {
            $fullDirectory = Get-CaseDirectory -Root $Root -Mode $mode -Corpus $corpus
            Invoke-BenchmarkCase -ExecutablePath $ExecutablePath -CaseDirectory $fullDirectory `
                -Mode $mode -Corpus $corpus -CaseSamples $FullSamples `
                -CaseWarmupFrames $FullWarmupFrames -CaseConfiguration $MatrixConfiguration
            for ($trial = 2; $trial -le $RequestedColdTrials; $trial++) {
                $coldDirectory = Get-CaseDirectory -Root $Root -Mode $mode -Corpus $corpus -Trial $trial
                Invoke-BenchmarkCase -ExecutablePath $ExecutablePath -CaseDirectory $coldDirectory `
                    -Mode $mode -Corpus $corpus -CaseSamples 30 -CaseWarmupFrames 10 `
                    -CaseConfiguration $MatrixConfiguration
            }
        }
    }
}

function Add-GateFailure {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Failures,
        [Parameter(Mandatory = $true)][string]$Message
    )

    $Failures.Add($Message)
}

function Test-ResultContract {
    param(
        [Parameter(Mandatory = $true)]$Record,
        [Parameter(Mandatory = $true)][string]$ExpectedMode,
        [Parameter(Mandatory = $true)][string]$ExpectedCorpus,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Failures,
        [switch]$RequireFullEvidence
    )

    $result = $Record.Result
    $label = "$ExpectedConfiguration/$ExpectedMode/$ExpectedCorpus"
    if ([int]$result.schema_version -ne $script:ResultSchemaVersion) {
        Add-GateFailure $Failures "$label schema is not $($script:ResultSchemaVersion)."
    }
    if (-not [bool]$result.success) {
        Add-GateFailure $Failures "$label runner reported failure."
    }
    if ([string]$result.mode -ne $ExpectedMode -or [string]$result.corpus -ne $ExpectedCorpus) {
        Add-GateFailure $Failures "$label identity does not match its result path."
    }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        Add-GateFailure $Failures "$label build configuration mismatch."
    }
    if (-not [bool]$result.cold_start_attribution.complete) {
        Add-GateFailure $Failures "$label cold-start attribution is incomplete."
    }
    if (-not [bool]$result.memory_evidence.second_view_created) {
        Add-GateFailure $Failures "$label did not create its second view."
    }
    if ($RequireFullEvidence) {
        if (-not [bool]$result.screenshot_exists -or
            -not (Test-Path -LiteralPath ([string]$result.screenshot) -PathType Leaf)) {
            Add-GateFailure $Failures "$label screenshot evidence is missing."
        }
        if ($ExpectedMode -eq "WebToUE" -and
            [double]$result.window_slate_batches.p50 -gt [double]$script:BatchCaps[$ExpectedCorpus]) {
            Add-GateFailure $Failures "$label exceeded its frozen Slate batch ceiling."
        }
        if ($ExpectedMode -eq "WebToUE" -and
            [double]$result.window_slate_vertices.p50 -gt [double]$script:VertexCaps[$ExpectedCorpus]) {
            Add-GateFailure $Failures "$label exceeded its frozen Slate vertex ceiling."
        }
    }
    if ($ExpectedMode -eq "WebToUE") {
        if (-not [bool]$result.product_policy.evaluated -or
            -not [bool]$result.product_policy.passed) {
            Add-GateFailure $Failures "$label failed the embedded WebToUE product policy."
        }
        if ([int]$result.compiled_document.compiled_resources -gt $script:MaximumCompiledResources) {
            Add-GateFailure $Failures "$label exceeded the frozen resource ceiling."
        }
        if ([uint64]$result.second_view_workload.'workload.hydrated_nodes' -ne
            [uint64]$result.compiled_document.compiled_nodes) {
            Add-GateFailure $Failures "$label second view is not K=1."
        }
        foreach ($workload in @($result.setup_workload, $result.measurement_workload,
            $result.second_view_workload)) {
            if ([uint64]$workload.'workload.resource_load_attempts' -ne 0) {
                Add-GateFailure $Failures "$label performed a synchronous resource load."
                break
            }
        }
    }
}

function Test-PackagedResults {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration,
        [Parameter(Mandatory = $true)][int]$RequestedColdTrials
    )

    $failures = New-Object 'System.Collections.Generic.List[string]'
    $fullRecords = @{}
    $comparisons = New-Object 'System.Collections.Generic.List[object]'
    $memoryComparisons = New-Object 'System.Collections.Generic.List[object]'
    $coldComparisons = New-Object 'System.Collections.Generic.List[object]'
    foreach ($mode in $script:Modes) {
        foreach ($corpus in $script:Corpora) {
            try {
                $record = Read-BenchmarkResult -Root $Root -Mode $mode -Corpus $corpus
                $fullRecords["$mode/$corpus"] = $record
                Test-ResultContract -Record $record -ExpectedMode $mode `
                    -ExpectedCorpus $corpus -ExpectedConfiguration $ExpectedConfiguration `
                    -Failures $failures -RequireFullEvidence
            }
            catch {
                Add-GateFailure $failures $_.Exception.Message
            }
        }
    }

    foreach ($corpus in $script:Corpora) {
        $webRecord = $fullRecords["WebToUE/$corpus"]
        $umgRecord = $fullRecords["UMG/$corpus"]
        if ($null -eq $webRecord -or $null -eq $umgRecord) {
            continue
        }
        foreach ($metric in @("game_thread_ms", "render_thread_ms", "gpu_ms",
            "input_to_backbuffer_ready_ms")) {
            try {
                $webValue = Get-PercentileValue -Result $webRecord.Result -Metric $metric
                $umgValue = Get-PercentileValue -Result $umgRecord.Result -Metric $metric
                if ($umgValue -le 0.0) {
                    throw "UMG baseline is not positive."
                }
                $ratio = $webValue / $umgValue
                $comparisons.Add([pscustomobject]@{
                    corpus = $corpus
                    metric = $metric
                    webtoue_p95 = $webValue
                    umg_p95 = $umgValue
                    ratio = $ratio
                    maximum_ratio = $script:MaximumEndToEndRatio
                    passed = ($ratio -le $script:MaximumEndToEndRatio)
                })
                if ($ratio -gt $script:MaximumEndToEndRatio) {
                    Add-GateFailure $failures "$ExpectedConfiguration/$corpus $metric exceeded 2x UMG."
                }
            }
            catch {
                Add-GateFailure $failures "$ExpectedConfiguration/$corpus $metric comparison failed: $($_.Exception.Message)"
            }
        }
        $rssDelta = [double]$webRecord.Result.rss_mib.p50 -
            [double]$umgRecord.Result.rss_mib.p50
        $llmDelta = $null
        $llmPassed = $null
        if ($ExpectedConfiguration -eq "Development") {
            if (-not [bool]$webRecord.Result.llm_enabled -or
                [string]$webRecord.Result.llm_availability -ne "available") {
                Add-GateFailure $failures "$ExpectedConfiguration/$corpus Development LLM is unavailable."
            }
            $llmDelta = [double]$webRecord.Result.llm_mib.p50 -
                [double]$umgRecord.Result.llm_mib.p50
            $llmPassed = $llmDelta -le $script:MaximumDevelopmentLlmDeltaMiB
            if (-not $llmPassed) {
                Add-GateFailure $failures "$ExpectedConfiguration/$corpus steady LLM delta exceeded 64 MiB."
            }
        }
        elseif ([bool]$webRecord.Result.llm_enabled -or
            [string]$webRecord.Result.llm_availability -ne "not_compiled_for_configuration") {
            Add-GateFailure $failures "$ExpectedConfiguration/$corpus must report Shipping LLM as not compiled."
        }
        $memoryComparisons.Add([pscustomobject]@{
            corpus = $corpus
            webtoue_rss_p50_mib = [double]$webRecord.Result.rss_mib.p50
            umg_rss_p50_mib = [double]$umgRecord.Result.rss_mib.p50
            raw_rss_delta_mib = $rssDelta
            rss_enforced = $false
            rss_reason = "Independent packaged processes have allocator/driver baseline drift; raw RSS is reported while same-process second-view deltas remain enforced by the embedded product policy."
            webtoue_llm_p50_mib = [double]$webRecord.Result.llm_mib.p50
            umg_llm_p50_mib = [double]$umgRecord.Result.llm_mib.p50
            llm_delta_mib = $llmDelta
            maximum_llm_delta_mib = $(if ($ExpectedConfiguration -eq "Development") {
                $script:MaximumDevelopmentLlmDeltaMiB
            } else { $null })
            llm_passed = $llmPassed
        })

        $webCold = New-Object 'System.Collections.Generic.List[double]'
        $umgCold = New-Object 'System.Collections.Generic.List[double]'
        for ($trial = 1; $trial -le $RequestedColdTrials; $trial++) {
            foreach ($mode in $script:Modes) {
                try {
                    $record = Read-BenchmarkResult -Root $Root -Mode $mode -Corpus $corpus -Trial $trial
                    Test-ResultContract -Record $record -ExpectedMode $mode `
                        -ExpectedCorpus $corpus -ExpectedConfiguration $ExpectedConfiguration `
                        -Failures $failures
                    if ($mode -eq "WebToUE") {
                        $webCold.Add([double]$record.Result.cold_first_frame_ms)
                    }
                    else {
                        $umgCold.Add([double]$record.Result.cold_first_frame_ms)
                    }
                }
                catch {
                    Add-GateFailure $failures $_.Exception.Message
                }
            }
        }
        if ($webCold.Count -eq $RequestedColdTrials -and
            $umgCold.Count -eq $RequestedColdTrials) {
            $webMedian = Get-Median -Values $webCold.ToArray()
            $umgMedian = Get-Median -Values $umgCold.ToArray()
            $ratio = if ($umgMedian -gt 0.0) { $webMedian / $umgMedian } else { [double]::PositiveInfinity }
            $coldComparisons.Add([pscustomobject]@{
                corpus = $corpus
                webtoue_trials_ms = $webCold.ToArray()
                umg_trials_ms = $umgCold.ToArray()
                webtoue_median_ms = $webMedian
                umg_median_ms = $umgMedian
                ratio = $ratio
                maximum_ratio = $script:MaximumEndToEndRatio
                passed = ($ratio -le $script:MaximumEndToEndRatio)
            })
            if ($ratio -gt $script:MaximumEndToEndRatio) {
                Add-GateFailure $failures "$ExpectedConfiguration/$corpus median cold start exceeded 2x UMG."
            }
        }
    }

    return [ordered]@{
        schema_version = $script:GateSchemaVersion
        success = ($failures.Count -eq 0)
        configuration = $ExpectedConfiguration
        generated_utc = [DateTime]::UtcNow.ToString("o")
        cold_trials = $RequestedColdTrials
        thresholds = [ordered]@{
            maximum_end_to_end_ratio = $script:MaximumEndToEndRatio
            maximum_development_llm_delta_mib = $script:MaximumDevelopmentLlmDeltaMiB
            maximum_compiled_resources = $script:MaximumCompiledResources
            resolution = "$($script:ResolutionX)x$($script:ResolutionY)"
            raw_rss_enforced = $false
            slate_batch_caps = $script:BatchCaps
            slate_vertex_caps = $script:VertexCaps
        }
        comparisons = $comparisons.ToArray()
        memory_comparisons = $memoryComparisons.ToArray()
        cold_comparisons = $coldComparisons.ToArray()
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Packaged gate executable must be a Win64 .exe: $resolvedExecutable"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        Invoke-BenchmarkMatrix -ExecutablePath $resolvedExecutable -Root $resolvedRoot `
            -FullSamples $Samples -FullWarmupFrames $WarmupFrames `
            -RequestedColdTrials $ColdTrials -MatrixConfiguration $Configuration
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Validation root does not exist: $resolvedRoot"
    }

    $summary = Test-PackagedResults -Root $resolvedRoot `
        -ExpectedConfiguration $Configuration -RequestedColdTrials $ColdTrials
    $summaryPath = Join-Path $resolvedRoot "gate.json"
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $summary | ConvertTo-Json -Depth 20
    if (-not $summary.success) {
        exit 3
    }
    exit 0
}
catch {
    [ordered]@{
        schema_version = $script:GateSchemaVersion
        success = $false
        configuration = $Configuration
        error = $_.Exception.Message
    } | ConvertTo-Json -Depth 5
    exit 2
}
