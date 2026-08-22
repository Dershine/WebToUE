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
        throw "Compositing smoke result is missing counter '$property'."
    }
    return [uint64]$Workload.$property
}

function Test-CompositingSmokeResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration
    )

    $resultPath = Join-Path $Root "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Compositing smoke did not write result.json: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ([int]$result.schema_version -ne 6) { $failures.Add("Result schema is not 6.") }
    if (-not [bool]$result.success) { $failures.Add("Packaged runner reported failure.") }
    if ([string]$result.mode -ne "WebToUE" -or
        [string]$result.corpus -ne "CompositingSmoke") {
        $failures.Add("Packaged runner identity is not CompositingSmoke.")
    }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        $failures.Add("Build configuration mismatch.")
    }
    if ([int]$result.compiled_document.compiled_resources -ne 1) {
        $failures.Add("CompositingSmoke must seal exactly one parent Material resource.")
    }
    if (-not [bool]$result.product_policy.evaluated -or
        -not [bool]$result.product_policy.passed) {
        $failures.Add("Embedded product policy failed.")
    }

    $material = $result.compiled_document.material_resource
    if ($null -eq $material -or -not [bool]$material.evaluated -or
        -not [bool]$material.passed -or [bool]$material.is_dynamic_instance) {
        $failures.Add("Sealed parent Material identity is incomplete.")
    }
    $parameter = $result.material_parameter
    if ($null -eq $parameter -or -not [bool]$parameter.evaluated -or
        -not [bool]$parameter.passed -or
        -not [bool]$parameter.warmup_committed -or
        -not [bool]$parameter.measurement_committed -or
        -not [bool]$parameter.second_view_committed) {
        $failures.Add("View-isolated MID transaction evidence is incomplete.")
    }
    $visual = $result.visual_transform
    if ($null -eq $visual -or -not [bool]$visual.evaluated -or
        -not [bool]$visual.passed -or -not [bool]$visual.semantic_target_found) {
        $failures.Add("Nested transform/clip and exact-hit evidence is incomplete.")
    }

    $compositing = $result.compositing
    if ($null -eq $compositing -or -not [bool]$compositing.evaluated -or
        -not [bool]$compositing.passed) {
        $failures.Add("Compositing evidence object failed.")
    }
    else {
        if ([uint64]$compositing.tier0_decisions -lt 1 -or
            [uint64]$compositing.tier1_decisions -lt 2 -or
            [uint64]$compositing.plan_rejections -ne 0) {
            $failures.Add("Tier 0/1 selection or fail-closed plan evidence is invalid.")
        }
        if ([string]$compositing.tier2_subtree_layer.status -ne "N/A" -or
            [bool]$compositing.tier2_subtree_layer.runtime_evaluated -or
            [uint64]$compositing.tier2_subtree_layer.decisions -ne 0 -or
            [string]$compositing.tier3_render_target.status -ne "N/A" -or
            [bool]$compositing.tier3_render_target.runtime_evaluated -or
            [uint64]$compositing.tier3_render_target.decisions -ne 0) {
            $failures.Add("Inactive Tier 2/3 must be explicit N/A, never zero-cost proof.")
        }
        if ([uint64]$compositing.active_layers -ne 0 -or
            [uint64]$compositing.active_surfaces -ne 0 -or
            [uint64]$compositing.allocated_pixels -ne 0 -or
            [uint64]$compositing.allocated_bytes -ne 0) {
            $failures.Add("Tier 0/1 fixture unexpectedly allocated an offscreen layer or surface.")
        }
        if ([uint64]$compositing.measurement_redraws -lt 1 -or
            [uint64]$compositing.measurement_passes -lt 1 -or
            [uint64]$compositing.measurement_commands -lt 1 -or
            [uint64]$compositing.measurement_display_patches -lt 1 -or
            [uint64]$compositing.measurement_spatial_patches -lt 1 -or
            [uint64]$compositing.measurement_dirty_rects -lt 1) {
            $failures.Add("Renderer-backed redraw/pass/command or local dirty evidence is missing.")
        }
        if ([uint64]$compositing.measurement_yoga_style_writes -ne 0 -or
            [uint64]$compositing.measurement_yoga_nodes_dirtied -ne 0 -or
            [uint64]$compositing.measurement_yoga_results_changed -ne 0) {
            $failures.Add("K=1 paint/hit work unexpectedly changed Yoga state.")
        }
        if (-not [bool]$compositing.shared_parent_material -or
            -not [bool]$compositing.primary_mid_committed -or
            -not [bool]$compositing.second_view_mid_committed) {
            $failures.Add("Static parent Material / View-owned MID isolation is incomplete.")
        }
    }

    $screenshotPath = [string]$result.screenshot
    $screenshotExists = [bool]$result.screenshot_exists -and
        (Test-Path -LiteralPath $screenshotPath -PathType Leaf)
    if (-not $screenshotExists) { $failures.Add("Visible screenshot evidence is missing.") }
    if ([int]$result.window_slate_batches.count -lt 1 -or
        [double]$result.window_slate_batches.p50 -le 0.0 -or
        [int]$result.window_slate_vertices.count -lt 1 -or
        [double]$result.window_slate_vertices.p50 -le 0.0 -or
        [int]$result.gpu_ms.count -lt 1 -or [double]$result.gpu_ms.p50 -le 0.0) {
        $failures.Add("Independent Slate batch/vertex/GPU evidence is missing.")
    }
    foreach ($workload in @($result.setup_workload, $result.warmup_workload,
            $result.measurement_workload, $result.second_view_workload)) {
        if ((Get-Counter $workload "resource_load_attempts") -ne 0 -or
            (Get-Counter $workload "resource_failures") -ne 0 -or
            (Get-Counter $workload "resource_cancellations") -ne 0) {
            $failures.Add("A synchronous resource load, failure, or cancellation was observed.")
            break
        }
    }

    $projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
    $candidateHead = (& git -C $projectRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $candidateHead -notmatch '^[0-9a-f]{40}$') {
        throw "Unable to resolve the candidate Git HEAD."
    }
    $corpusPath = Join-Path $projectRoot "WebUI\Examples\CompositingSmoke.html"
    $corpusHash = (Get-FileHash -LiteralPath $corpusPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $screenshotHash = if ($screenshotExists) {
        (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { "" }

    return [ordered]@{
        schema_version = 1
        success = ($failures.Count -eq 0)
        configuration = $ExpectedConfiguration
        generated_utc = [DateTime]::UtcNow.ToString("o")
        candidate_head = $candidateHead
        corpus = "CompositingSmoke"
        corpus_sha256 = $corpusHash
        tier_operation_ids = @(
            "display-rebuild/deterministic-plan",
            "material-submit/warmup-primary-mid",
            "material-submit/measurement-primary-mid-reuse",
            "material-submit/second-view-isolated-mid",
            "pointer-hover/exact-hit-local-dirty"
        )
        workload = [ordered]@{
            compiled_nodes = [int]$result.compiled_document.compiled_nodes
            compiled_resources = [int]$result.compiled_document.compiled_resources
            measurement_redraws = [uint64]$compositing.measurement_redraws
            measurement_passes = [uint64]$compositing.measurement_passes
            measurement_commands = [uint64]$compositing.measurement_commands
        }
        resource = [ordered]@{
            path = [string]$material.path
            shared_parent = [bool]$compositing.shared_parent_material
            primary_mid_committed = [bool]$compositing.primary_mid_committed
            second_view_mid_committed = [bool]$compositing.second_view_mid_committed
        }
        screenshot = $screenshotPath
        screenshot_sha256 = $screenshotHash
        result = $resultPath
        evidence_boundaries = @(
            "BuildCookRun proves build/cook/stage/pak/iostore and relaunch gates only.",
            "This gate independently runs the inner packaged binary and records renderer-backed numeric and visual evidence.",
            "Tier 2/3 are N/A for the sealed product fixture; the Editor adversarial prototype is separate evidence, not packaged cost proof."
        )
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Compositing smoke executable must be a Win64 .exe: $resolvedExecutable"
        }
        if ([IO.Path]::GetFileName($resolvedExecutable) -eq "WebToUE.exe") {
            $stagedBinary = Join-Path (Split-Path -Parent $resolvedExecutable) `
                "WebToUE\Binaries\Win64\WebToUE-Win64-$Configuration.exe"
            if (Test-Path -LiteralPath $stagedBinary -PathType Leaf) {
                $resolvedExecutable = (Resolve-Path -LiteralPath $stagedBinary).Path
            }
        }
        if (Test-Path -LiteralPath (Join-Path $resolvedRoot "result.json")) {
            throw "Refusing to overwrite existing compositing evidence: $resolvedRoot"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        $logPath = Join-Path $resolvedRoot "WebToUE.log"
        $arguments = @(
            "-WTUEBenchmark=WebToUE",
            "-WTUECorpus=CompositingSmoke",
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
            throw "Compositing smoke process failed with exit code $($process.ExitCode)."
        }
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Compositing smoke validation root does not exist: $resolvedRoot"
    }

    $summary = Test-CompositingSmokeResult -Root $resolvedRoot `
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
