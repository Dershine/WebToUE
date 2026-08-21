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
        throw "Material smoke result is missing counter '$property'."
    }
    return [uint64]$Workload.$property
}

function Test-MaterialSmokeResult {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedConfiguration
    )

    $resultPath = Join-Path $Root "result.json"
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Material smoke did not write result.json: $resultPath"
    }
    $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ([int]$result.schema_version -ne 6) { $failures.Add("Result schema is not 6.") }
    if (-not [bool]$result.success) { $failures.Add("Packaged runner reported failure.") }
    if ([string]$result.mode -ne "WebToUE" -or
        [string]$result.corpus -ne "ResourceMaterialSmoke") {
        $failures.Add("Packaged runner identity is not the Material smoke fixture.")
    }
    if ([string]$result.build_configuration -ne $ExpectedConfiguration) {
        $failures.Add("Build configuration mismatch.")
    }
    if ([int]$result.compiled_document.compiled_resources -ne 1) {
        $failures.Add("Material smoke does not contain exactly one compiled resource.")
    }

    $expectedPath = "/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush.MI_WTUE_StaticMaterialBrush"
    $expectedDependency = "asset/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush"
    $material = $result.compiled_document.material_resource
    if ($null -eq $material -or -not [bool]$material.evaluated -or
        -not [bool]$material.passed) {
        $failures.Add("Packaged static Material identity contract failed.")
    }
    elseif ([string]$material.origin -ne "UnrealAsset" -or
        [string]$material.author_reference -ne $expectedPath -or
        [string]$material.path -ne $expectedPath -or
        [string]$material.resolved_dependency_id -ne $expectedDependency -or
        -not ([string]$material.resource_id).StartsWith("resource/material/") -or
        [string]$material.residency -ne "Critical" -or
        [double]$material.brush_width -le 0.0 -or
        [double]$material.brush_height -le 0.0 -or
        -not [bool]$material.is_material_interface -or
        [bool]$material.is_dynamic_instance) {
        $failures.Add("Packaged static Material metadata is incomplete or invalid.")
    }
    if (-not [bool]$result.product_policy.evaluated -or
        -not [bool]$result.product_policy.passed) {
        $failures.Add("Embedded Material smoke policy failed.")
    }
    if (-not [bool]$result.screenshot_exists -or
        -not (Test-Path -LiteralPath ([string]$result.screenshot) -PathType Leaf)) {
        $failures.Add("Visible Material screenshot evidence is missing.")
    }

    $setup = $result.setup_workload
    $warmup = $result.warmup_workload
    $measurement = $result.measurement_workload
    $second = $result.second_view_workload
    $primaryConsumptions =
        (Get-Counter $setup "resource_async_requests") +
        (Get-Counter $setup "resource_cache_hits") +
        (Get-Counter $warmup "resource_async_requests") +
        (Get-Counter $warmup "resource_cache_hits")
    if ($primaryConsumptions -ne 1) {
        $failures.Add("Primary View did not consume exactly one Material.")
    }
    if ((Get-Counter $setup "brush_builds") +
        (Get-Counter $warmup "brush_builds") -lt 1) {
        $failures.Add("Primary View did not build a Slate Material brush.")
    }
    foreach ($workload in @($setup, $warmup, $measurement, $second)) {
        if ((Get-Counter $workload "resource_load_attempts") -ne 0) {
            $failures.Add("A synchronous Material load was observed.")
            break
        }
    }
    foreach ($workload in @($setup, $warmup, $measurement, $second)) {
        if ((Get-Counter $workload "resource_failures") -ne 0 -or
            (Get-Counter $workload "resource_cancellations") -ne 0) {
            $failures.Add("A Material failure or cancellation was observed.")
            break
        }
    }
    if ((Get-Counter $measurement "resource_async_requests") -ne 0) {
        $failures.Add("Measurement started a new Material request.")
    }
    if ((Get-Counter $second "resource_async_requests") -ne 0 -or
        (Get-Counter $second "resource_cache_hits") -ne 1) {
        $failures.Add("Second View did not reuse exactly one resident Material.")
    }

    return [ordered]@{
        schema_version = 1
        success = ($failures.Count -eq 0)
        configuration = $ExpectedConfiguration
        generated_utc = [DateTime]::UtcNow.ToString("o")
        result = $resultPath
        screenshot = [string]$result.screenshot
        compiled_resources = [int]$result.compiled_document.compiled_resources
        material_resource_id = [string]$material.resource_id
        material_path = [string]$material.path
        material_origin = [string]$material.origin
        material_residency = [string]$material.residency
        material_brush_width = [double]$material.brush_width
        material_brush_height = [double]$material.brush_height
        material_is_dynamic_instance = [bool]$material.is_dynamic_instance
        primary_resource_consumptions = $primaryConsumptions
        primary_async_requests =
            (Get-Counter $setup "resource_async_requests") +
            (Get-Counter $warmup "resource_async_requests")
        primary_cache_hits =
            (Get-Counter $setup "resource_cache_hits") +
            (Get-Counter $warmup "resource_cache_hits")
        second_view_cache_hits = Get-Counter $second "resource_cache_hits"
        synchronous_loads =
            (Get-Counter $setup "resource_load_attempts") +
            (Get-Counter $warmup "resource_load_attempts") +
            (Get-Counter $measurement "resource_load_attempts") +
            (Get-Counter $second "resource_load_attempts")
        failures = $failures.ToArray()
    }
}

try {
    $resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
    if (-not $ValidateOnly) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
        if ([IO.Path]::GetExtension($resolvedExecutable) -ne ".exe") {
            throw "Material smoke executable must be a Win64 .exe: $resolvedExecutable"
        }
        if (Test-Path -LiteralPath (Join-Path $resolvedRoot "result.json")) {
            throw "Refusing to overwrite existing Material smoke evidence: $resolvedRoot"
        }
        New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
        $logPath = Join-Path $resolvedRoot "WebToUE.log"
        $arguments = @(
            "-WTUEBenchmark=WebToUE",
            "-WTUECorpus=ResourceMaterialSmoke",
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
            throw "Material smoke process failed with exit code $($process.ExitCode)."
        }
    }
    elseif (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        throw "Material smoke validation root does not exist: $resolvedRoot"
    }

    $summary = Test-MaterialSmokeResult -Root $resolvedRoot `
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
