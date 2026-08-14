$ScriptUnderTest = Join-Path $PSScriptRoot "..\scripts\Invoke-WebToUEEditorLifecycle.ps1"

function New-FakeLifecycleEnvironment {
    param([string]$Root)

    $projectRoot = Join-Path $Root "Project"
    $engineRoot = Join-Path $Root "UE_5.8"
    New-Item -ItemType Directory -Force -Path `
        (Join-Path $projectRoot "Plugins\VibeUE"), `
        (Join-Path $projectRoot "Saved"), `
        (Join-Path $projectRoot "Intermediate"), `
        (Join-Path $engineRoot "Engine\Build\BatchFiles"), `
        (Join-Path $engineRoot "Engine\Binaries\Win64"), `
        (Join-Path $engineRoot "Engine\Intermediate") | Out-Null

    Set-Content -LiteralPath (Join-Path $projectRoot "TestProject.uproject") -Value '{"EngineAssociation":"5.8"}' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $projectRoot "Plugins\VibeUE\BuildAndLaunchGame.ps1") -Value '$enginePath = $null' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $engineRoot "Engine\Build\BatchFiles\Build.bat") -Value '@exit /b 0' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $engineRoot "Engine\Build\BatchFiles\RunUAT.bat") -Value '@exit /b 0' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $engineRoot "Engine\Binaries\Win64\UnrealEditor.exe") -Value '' -Encoding ASCII

    return [pscustomobject]@{
        ProjectRoot = $projectRoot
        EngineRoot = $engineRoot
    }
}

Describe "WebToUE Editor lifecycle preflight" {
    It "passes all probes in a writable isolated environment" {
        $environment = New-FakeLifecycleEnvironment -Root $TestDrive
        $localData = Join-Path $TestDrive "LocalData"
        New-Item -ItemType Directory -Force -Path `
            (Join-Path $localData "UnrealBuildTool"), `
            (Join-Path $localData "UnrealEngine") | Out-Null

        $previousLocalAppData = $env:LOCALAPPDATA
        try {
            $env:LOCALAPPDATA = $localData
            $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
                -Action Preflight -ProjectRoot $environment.ProjectRoot -EngineRoot $environment.EngineRoot
            $exitCode = $LASTEXITCODE
        }
        finally {
            $env:LOCALAPPDATA = $previousLocalAppData
        }

        $exitCode | Should Be 0
        (($output -join "`n") | ConvertFrom-Json).Passed | Should Be $true
        @(Get-ChildItem -LiteralPath $TestDrive -Filter ".webtoue-write-probe-*.tmp" -Recurse -File).Count | Should Be 0
    }

    It "fails before creating operation state when an exact probe directory is missing" {
        $environment = New-FakeLifecycleEnvironment -Root $TestDrive
        Remove-Item -LiteralPath (Join-Path $environment.EngineRoot "Engine\Intermediate") -Force
        $localData = Join-Path $TestDrive "LocalData"
        New-Item -ItemType Directory -Force -Path `
            (Join-Path $localData "UnrealBuildTool"), `
            (Join-Path $localData "UnrealEngine") | Out-Null

        $previousLocalAppData = $env:LOCALAPPDATA
        try {
            $env:LOCALAPPDATA = $localData
            $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptUnderTest `
                -Action Preflight -ProjectRoot $environment.ProjectRoot -EngineRoot $environment.EngineRoot
            $exitCode = $LASTEXITCODE
        }
        finally {
            $env:LOCALAPPDATA = $previousLocalAppData
        }

        $result = ($output -join "`n") | ConvertFrom-Json
        $exitCode | Should Be 5
        $result.Passed | Should Be $false
        $result.FailedCount | Should Be 1
        (Test-Path -LiteralPath (Join-Path $environment.ProjectRoot "Saved\VibeUE\Lifecycle\operation.json")) | Should Be $false
    }

    It "detects a persisted live owner and removes only exact lifecycle-script artifacts" {
        $environment = New-FakeLifecycleEnvironment -Root $TestDrive
        $vibeScript = Join-Path $environment.ProjectRoot "Plugins\VibeUE\BuildAndLaunchGame.ps1"
        $staleScript = Join-Path $environment.ProjectRoot "Plugins\VibeUE\.BuildAndLaunchGame.WebToUE.stale.ps1"
        Set-Content -LiteralPath $staleScript `
            -Value "stale lifecycle artifact" -Encoding UTF8

        $scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
            (Resolve-Path $ScriptUnderTest), [ref]$null, [ref]$null)
        foreach ($functionName in @("Get-ActiveOperationPids", "Remove-StaleLifecycleScripts")) {
            $functionAst = $scriptAst.Find({
                param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
            }, $true)
            Invoke-Expression $functionAst.Extent.Text
        }

        $operation = [pscustomobject]@{ Phase = "InvokingVibeUE"; OwnerPid = $PID; ReleaseHostPid = $null; ReleaseProcessPids = @(); VibeProcessPid = $null; NewEditorPid = $null }
        @(Get-ActiveOperationPids -Operation $operation) | Should Be @($PID)

        $releaseOperation = [pscustomobject]@{ Phase = "RunningBuildCookRun"; OwnerPid = $null; ReleaseHostPid = $null; ReleaseProcessPids = @($PID); VibeProcessPid = $null; NewEditorPid = $null }
        @(Get-ActiveOperationPids -Operation $releaseOperation) | Should Be @($PID)
        @(Remove-StaleLifecycleScripts -VibeScriptPath $vibeScript).Count | Should Be 1
        (Test-Path -LiteralPath $staleScript) | Should Be $false
        (Test-Path -LiteralPath $vibeScript) | Should Be $true
    }

    It "constructs the fixed tracked BuildCookRun contract" {
        $scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
            (Resolve-Path $ScriptUnderTest), [ref]$null, [ref]$null)
        foreach ($functionName in @("Get-BuildCookRunArguments", "ConvertTo-PowerShellSingleQuotedLiteral")) {
            $functionAst = $scriptAst.Find({
                param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
            }, $true)
            Invoke-Expression $functionAst.Extent.Text
        }

        $arguments = @(Get-BuildCookRunArguments `
            -ProjectFile "E:\Projects\Example Project\Example.uproject" `
            -Platform "Win64" `
            -Configuration "Development" `
            -McpPort 8001)

        ($arguments -contains "BuildCookRun") | Should Be $true
        ($arguments -contains "-Build") | Should Be $true
        ($arguments -contains "-Cook") | Should Be $true
        ($arguments -contains "-Stage") | Should Be $true
        ($arguments -contains "-Pak") | Should Be $true
        ($arguments -contains "-IoStore") | Should Be $true
        ($arguments -contains "-AdditionalCookerOptions=-ModelContextProtocolPort=8001") | Should Be $true
        ($arguments -join " ") | Should Not Match "ModelContextProtocolPort=8000"
        (ConvertTo-PowerShellSingleQuotedLiteral -Value "C:\It's Here") | Should Be "'C:\It''s Here'"
    }

    It "uses the final AutomationTool log exit code instead of a false-zero host" {
        $scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
            (Resolve-Path $ScriptUnderTest), [ref]$null, [ref]$null)
        $functionAst = $scriptAst.Find({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq "Get-AutomationToolExitCodeFromOutput"
        }, $true)
        Invoke-Expression $functionAst.Extent.Text

        Get-AutomationToolExitCodeFromOutput -OutputLines @(
            "AutomationTool exiting with ExitCode=0 (Success)",
            "AutomationTool exiting with ExitCode=6 (6)") | Should Be 6
        Get-AutomationToolExitCodeFromOutput -OutputLines @(
            "BUILD SUCCESSFUL",
            "AutomationTool exiting with ExitCode=0 (Success)") | Should Be 0
        Get-AutomationToolExitCodeFromOutput -OutputLines @("no marker") | Should BeNullOrEmpty
    }

    It "limits MCP startup recovery to one delayed recheck" {
        $scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
            (Resolve-Path $ScriptUnderTest), [ref]$null, [ref]$null)
        $functionAst = $scriptAst.Find({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq "Test-McpEndpointWithRetry"
        }, $true)
        Invoke-Expression $functionAst.Extent.Text

        $script:mcpAttempts = 0
        function Test-McpEndpoint {
            $script:mcpAttempts++
            return [pscustomobject]@{ Ready = ($script:mcpAttempts -eq 2); Error = $null }
        }
        function Start-Sleep { param([int]$Seconds) }

        $result = Test-McpEndpointWithRetry -Uri "http://127.0.0.1:8000/mcp" -TimeoutSec 1 -RetryDelaySec 1
        $result.Ready | Should Be $true
        $result.AttemptCount | Should Be 2
        $script:mcpAttempts | Should Be 2
    }
}
