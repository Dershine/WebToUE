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
        throw "Transform/clip smoke result is missing counter '$property'."
    }
    return [uint64]$Workload.$property
}

function Test-TransformClipSmokeResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration
    )

    $resultPath = Join-Path $Root "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Transform/clip smoke did not write result.json: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ([int]$result.schema_version -ne 6) { $failures.Add("Result schema is not 6.") }
    if (-not [bool]$result.success) { $failures.Add("Packaged runner reported failure.") }
    if ([string]$result.mode -ne "WebToUE" -or
        [string]$result.corpus -ne "TransformClipSmoke") {
        $failures.Add("Packaged runner identity is not the transform/clip fixture.")
    }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        $failures.Add("Build configuration mismatch.")
    }
    if ([int]$result.compiled_document.compiled_resources -ne 0) {
        $failures.Add("Transform/clip smoke unexpectedly compiled a Runtime resource.")
    }
    if (-not [bool]$result.product_policy.evaluated -or
        -not [bool]$result.product_policy.passed) {
        $failures.Add("Embedded transform/clip product policy failed.")
    }
    if (-not [bool]$result.screenshot_exists -or
        -not (Test-Path -LiteralPath ([string]$result.screenshot) -PathType Leaf)) {
        $failures.Add("Visible transform/clip screenshot evidence is missing.")
    }

    $visual = $result.visual_transform
    if ($null -eq $visual -or -not [bool]$visual.evaluated -or
        -not [bool]$visual.passed -or -not [bool]$visual.semantic_target_found) {
        $failures.Add("Transform/clip Runtime evidence is incomplete.")
    }
    else {
        $bounds = $visual.semantic_bounds
        if ($null -eq $bounds -or
            [double]$bounds.right -le [double]$bounds.left -or
            [double]$bounds.bottom -le [double]$bounds.top) {
            $failures.Add("Transformed semantic bounds are empty or invalid.")
        }
        if ([uint64]$visual.warmup_transform_commands -lt 3 -or
            [uint64]$visual.warmup_clip_zones -lt 2) {
            $failures.Add("Warmup did not resolve the controlled transform/clip fixture.")
        }
        if ([uint64]$visual.measurement_transform_commands -lt 1 -or
            [uint64]$visual.measurement_clip_zones -lt 2 -or
            [uint64]$visual.measurement_inverse_hit_tests -lt 1 -or
            [uint64]$visual.measurement_exact_clip_tests -lt 2 -or
            [uint64]$visual.measurement_display_patches -lt 1 -or
            [uint64]$visual.measurement_spatial_patches -lt 1 -or
            [uint64]$visual.measurement_dirty_rects -lt 1) {
            $failures.Add("Measurement did not exercise inverse hit, exact clips, and local display/spatial patching.")
        }
        if ([uint64]$visual.measurement_yoga_style_writes -ne 0 -or
            [uint64]$visual.measurement_yoga_nodes_dirtied -ne 0 -or
            [uint64]$visual.measurement_yoga_results_changed -ne 0) {
            $failures.Add("Paint/hit-only transform mutation unexpectedly changed Yoga state.")
        }
        if ([uint64]$visual.second_view_transform_commands -lt 3 -or
            [uint64]$visual.second_view_clip_zones -lt 2) {
            $failures.Add("Second View did not independently resolve transform/clip state.")
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
        semantic_bounds = $visual.semantic_bounds
        warmup_transform_commands = [uint64]$visual.warmup_transform_commands
        warmup_clip_zones = [uint64]$visual.warmup_clip_zones
        measurement_transform_commands = [uint64]$visual.measurement_transform_commands
        measurement_clip_zones = [uint64]$visual.measurement_clip_zones
        measurement_inverse_hit_tests = [uint64]$visual.measurement_inverse_hit_tests
        measurement_exact_clip_tests = [uint64]$visual.measurement_exact_clip_tests
        measurement_display_patches = [uint64]$visual.measurement_display_patches
        measurement_spatial_patches = [uint64]$visual.measurement_spatial_patches
        measurement_dirty_rects = [uint64]$visual.measurement_dirty_rects
        second_view_transform_commands = [uint64]$visual.second_view_transform_commands
        second_view_clip_zones = [uint64]$visual.second_view_clip_zones
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Transform/clip smoke executable must be a Win64 .exe: $resolvedExecutable"
        }
        if ([IO.Path]::GetFileName($resolvedExecutable) -eq "WebToUE.exe") {
            $stagedBinary = Join-Path (Split-Path -Parent $resolvedExecutable) `
                "WebToUE\Binaries\Win64\WebToUE-Win64-$Configuration.exe"
            if (Test-Path -LiteralPath $stagedBinary -PathType Leaf) {
                $resolvedExecutable = (Resolve-Path -LiteralPath $stagedBinary).Path
            }
        }
        if (Test-Path -LiteralPath (Join-Path $resolvedRoot "result.json")) {
            throw "Refusing to overwrite existing transform/clip evidence: $resolvedRoot"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        $logPath = Join-Path $resolvedRoot "WebToUE.log"
        $arguments = @(
            "-WTUEBenchmark=WebToUE",
            "-WTUECorpus=TransformClipSmoke",
            "-WTUEWarmupFrames=120",
            "-WTUESamples=120",
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
            throw "Transform/clip smoke process failed with exit code $($process.ExitCode)."
        }
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Transform/clip smoke validation root does not exist: $resolvedRoot"
    }

    $summary = Test-TransformClipSmokeResult -Root $resolvedRoot `
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
