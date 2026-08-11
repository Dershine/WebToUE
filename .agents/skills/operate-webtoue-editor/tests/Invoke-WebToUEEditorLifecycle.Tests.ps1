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

        $operation = [pscustomobject]@{ Phase = "InvokingVibeUE"; OwnerPid = $PID; VibeProcessPid = $null; NewEditorPid = $null }
        @(Get-ActiveOperationPids -Operation $operation) | Should Be @($PID)
        @(Remove-StaleLifecycleScripts -VibeScriptPath $vibeScript).Count | Should Be 1
        (Test-Path -LiteralPath $staleScript) | Should Be $false
        (Test-Path -LiteralPath $vibeScript) | Should Be $true
    }
}
