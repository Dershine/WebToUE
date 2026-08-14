$ScriptUnderTest = Join-Path $PSScriptRoot "..\Invoke-WebToUEPackagedExitGate.ps1"

function New-FakeResult {
    param(
        [string]$Root,
        [string]$Configuration,
        [string]$Mode,
        [string]$Corpus,
        [int]$Trial,
        [double]$ColdMs = 20.0
    )

    $relative = if ($Trial -eq 1) {
        "Full\$Mode-$Corpus"
    }
    else {
        "Cold\Trial-{0:D2}\$Mode-$Corpus" -f $Trial
    }
    $directory = Join-Path $Root $relative
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $screenshot = Join-Path $directory "$Mode-$Corpus.png"
    Set-Content -LiteralPath $screenshot -Value "fixture" -Encoding ASCII
    $nodes = if ($Corpus -eq "MainMenu") { 15 } elseif ($Corpus -eq "HUD") { 13 } else { 21 }
    $batches = if ($Corpus -eq "MainMenu") { 10 } elseif ($Corpus -eq "HUD") { 5 } else { 13 }
    $vertices = if ($Corpus -eq "MainMenu") { 492 } elseif ($Corpus -eq "HUD") { 136 } else { 384 }
    $scale = if ($Mode -eq "WebToUE") { 1.5 } else { 1.0 }
    $result = [ordered]@{
        schema_version = 6
        success = $true
        mode = $Mode
        corpus = $Corpus
        build_configuration = $Configuration
        screenshot_exists = $true
        screenshot = $screenshot
        cold_first_frame_ms = $ColdMs
        cold_start_attribution = @{ complete = $true }
        compiled_document = @{ compiled_nodes = $nodes; compiled_resources = 0 }
        product_policy = @{ evaluated = ($Mode -eq "WebToUE"); passed = $true }
        memory_evidence = @{ second_view_created = $true; second_view_rss_delta_mib = 1.0 }
        setup_workload = @{ 'workload.resource_load_attempts' = 0 }
        measurement_workload = @{
            'workload.hydrated_nodes' = 0
            'workload.resource_load_attempts' = 0
        }
        second_view_workload = @{
            'workload.hydrated_nodes' = $nodes
            'workload.resource_load_attempts' = 0
        }
        game_thread_ms = @{ p95 = 4.0 * $scale }
        render_thread_ms = @{ p95 = 6.0 * $scale }
        gpu_ms = @{ p95 = 8.0 * $scale }
        input_to_backbuffer_ready_ms = @{ p95 = 20.0 * $scale }
        window_slate_batches = @{ p50 = $batches }
        window_slate_vertices = @{ p50 = $vertices }
        rss_mib = @{ p50 = $(if ($Mode -eq "WebToUE") { 1020.0 } else { 1000.0 }) }
        llm_mib = @{ p50 = $(if ($Mode -eq "WebToUE") { 2020.0 } else { 2000.0 }) }
        llm_enabled = ($Configuration -eq "Development")
        llm_availability = $(if ($Configuration -eq "Development") { "available" } else { "not_compiled_for_configuration" })
    }
    $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory "result.json") -Encoding UTF8
}

function New-FakeMatrix {
    param([string]$Root, [string]$Configuration = "Development", [int]$ColdTrials = 3)

    foreach ($mode in @("WebToUE", "UMG")) {
        foreach ($corpus in @("MainMenu", "HUD", "ScrollableSettings")) {
            for ($trial = 1; $trial -le $ColdTrials; $trial++) {
                $cold = if ($mode -eq "WebToUE") { 20.0 } else { 15.0 }
                New-FakeResult -Root $Root -Configuration $Configuration -Mode $mode `
                    -Corpus $corpus -Trial $trial -ColdMs $cold
            }
        }
    }
}

Describe "WebToUE packaged exit gate" {
    It "constructs the fixed packaged benchmark command line" {
        $scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
            (Resolve-Path $ScriptUnderTest), [ref]$null, [ref]$null)
        $functionAst = $scriptAst.Find({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq "Get-BenchmarkArguments"
        }, $true)
        Invoke-Expression $functionAst.Extent.Text
        $script:ResolutionX = 1920
        $script:ResolutionY = 1080

        $arguments = @(Get-BenchmarkArguments -CaseDirectory "E:\Evidence\Case" `
            -Mode WebToUE -Corpus MainMenu -CaseSamples 600 -CaseWarmupFrames 120 `
            -CaseConfiguration Development)
        ($arguments -contains "-WTUEBenchmark=WebToUE") | Should Be $true
        ($arguments -contains "-WTUECorpus=MainMenu") | Should Be $true
        ($arguments -contains "-WTUEWarmupFrames=120") | Should Be $true
        ($arguments -contains "-WTUESamples=600") | Should Be $true
        ($arguments -contains "-WTUEOutput=E:\Evidence\Case") | Should Be $true
        ($arguments -contains "-abslog=E:\Evidence\Case\WebToUE.log") | Should Be $true
        ($arguments -contains "-ResX=1920") | Should Be $true
        ($arguments -contains "-ResY=1080") | Should Be $true
        ($arguments -contains "-NoVSync") | Should Be $true
        ($arguments -contains "-unattended") | Should Be $true
        ($arguments -contains "-LLM") | Should Be $true

        $shippingArguments = @(Get-BenchmarkArguments -CaseDirectory "E:\Evidence\Case" `
            -Mode WebToUE -Corpus MainMenu -CaseSamples 600 -CaseWarmupFrames 120 `
            -CaseConfiguration Shipping)
        ($shippingArguments -contains "-LLM") | Should Be $false

        $scriptSource = Get-Content -Raw -LiteralPath $ScriptUnderTest
        $scriptSource | Should Not Match "-WindowStyle Hidden"
        ($scriptSource -match '\$script:ColdTrialSamples = 60') | Should Be $true
        ($scriptSource -match '\$script:ColdTrialWarmupFrames = 10') | Should Be $true
    }

    It "accepts a complete Development matrix and preserves Shipping LLM semantics" {
        $developmentRoot = Join-Path $TestDrive "Development"
        New-FakeMatrix -Root $developmentRoot
        $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
            -ValidateOnly -Configuration Development -OutputRoot $developmentRoot -ColdTrials 3
        $exitCode = $LASTEXITCODE
        $summary = ($output -join "`n") | ConvertFrom-Json
        $exitCode | Should Be 0
        $summary.success | Should Be $true
        $summary.comparisons.Count | Should Be 12
        $summary.cold_comparisons.Count | Should Be 3

        $shippingRoot = Join-Path $TestDrive "Shipping"
        New-FakeMatrix -Root $shippingRoot -Configuration Shipping
        & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
            -ValidateOnly -Configuration Shipping -OutputRoot $shippingRoot -ColdTrials 3 | Out-Null
        $LASTEXITCODE | Should Be 0
    }

    It "uses a cold-start median so one isolated outlier does not fail the route" {
        $root = Join-Path $TestDrive "Outlier"
        New-FakeMatrix -Root $root
        $path = Join-Path $root "Cold\Trial-02\WebToUE-MainMenu\result.json"
        $result = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
        $result.cold_first_frame_ms = 400.0
        $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8

        & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
            -ValidateOnly -Configuration Development -OutputRoot $root -ColdTrials 3 | Out-Null
        $LASTEXITCODE | Should Be 0
    }

    It "rejects a persistent greater-than-2x timing regression" {
        $root = Join-Path $TestDrive "Regression"
        New-FakeMatrix -Root $root
        $path = Join-Path $root "Full\WebToUE-HUD\result.json"
        $result = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
        $result.game_thread_ms.p95 = 9.0
        $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8

        $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
            -ValidateOnly -Configuration Development -OutputRoot $root -ColdTrials 3
        $exitCode = $LASTEXITCODE
        $summary = ($output -join "`n") | ConvertFrom-Json
        $exitCode | Should Be 3
        $summary.success | Should Be $false
        ($summary.failures -join " ") | Should Match "exceeded 2x UMG"
    }

    It "reports independent-process RSS drift but enforces Development LLM" {
        $root = Join-Path $TestDrive "Memory"
        New-FakeMatrix -Root $root
        $webPath = Join-Path $root "Full\WebToUE-MainMenu\result.json"
        $web = Get-Content -Raw -LiteralPath $webPath | ConvertFrom-Json
        $web.rss_mib.p50 = 1800.0
        $web | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $webPath -Encoding UTF8

        $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
            -ValidateOnly -Configuration Development -OutputRoot $root -ColdTrials 3
        $summary = ($output -join "`n") | ConvertFrom-Json
        $LASTEXITCODE | Should Be 0
        $summary.memory_comparisons[0].rss_enforced | Should Be $false

        $web.llm_mib.p50 = 2100.0
        $web | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $webPath -Encoding UTF8
        $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
            -ValidateOnly -Configuration Development -OutputRoot $root -ColdTrials 3
        $summary = ($output -join "`n") | ConvertFrom-Json
        $LASTEXITCODE | Should Be 3
        ($summary.failures -join " ") | Should Match "LLM delta exceeded 64 MiB"
    }
}
