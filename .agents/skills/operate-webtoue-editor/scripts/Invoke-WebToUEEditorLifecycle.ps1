[CmdletBinding()]
param(
    [ValidateSet("Status", "SafeBuildAndLaunch")]
    [string]$Action = "Status",
    [string]$ProjectRoot,
    [string]$McpUri = "http://127.0.0.1:8000/mcp",
    [switch]$AssetsSaved,
    [switch]$StrictRebuild,
    [switch]$Clean,
    [switch]$SkipBuild,
    [ValidateRange(5, 300)]
    [int]$GracefulCloseTimeoutSec = 60,
    [ValidateRange(10, 600)]
    [int]$ReadyTimeoutSec = 180,
    [ValidateRange(1, 30)]
    [int]$McpTimeoutSec = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Stop-Lifecycle {
    param([int]$Code, [string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit $Code
}

function Resolve-ProjectRoot {
    param([string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return (Resolve-Path -LiteralPath $RequestedRoot).Path
    }

    $candidate = $PSScriptRoot
    for ($i = 0; $i -lt 4; $i++) {
        $candidate = Split-Path -Parent $candidate
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

function Get-UnrealEditors {
    return @(Get-Process -Name "UnrealEditor*" -ErrorAction SilentlyContinue | Sort-Object Id)
}

function Get-ReadySignalEvidence {
    param([string]$Root, [int]$EditorPid)

    $signalPath = Join-Path $Root "Saved\VibeUE\Signals\editor-$EditorPid-true.json"
    $evidence = [ordered]@{
        Path = $signalPath
        Exists = $false
        Valid = $false
        Reason = "missing"
        PluginVersion = $null
        SessionStartUtc = $null
    }

    if (-not (Test-Path -LiteralPath $signalPath)) {
        return [pscustomobject]$evidence
    }

    $evidence.Exists = $true
    try {
        $payload = Get-Content -Raw -LiteralPath $signalPath | ConvertFrom-Json
        if ([int]$payload.pid -ne $EditorPid) {
            $evidence.Reason = "pid-mismatch"
            return [pscustomobject]$evidence
        }
        if ([string]$payload.signal -ne "toolsets-registered") {
            $evidence.Reason = "unexpected-signal"
            return [pscustomobject]$evidence
        }

        $process = Get-Process -Id $EditorPid -ErrorAction Stop
        $processStartUtc = $process.StartTime.ToUniversalTime()
        $sessionStartUtc = [DateTimeOffset]::Parse([string]$payload.sessionStartUtc).UtcDateTime
        if ($sessionStartUtc -lt $processStartUtc.AddSeconds(-10)) {
            $evidence.Reason = "stale-session"
            return [pscustomobject]$evidence
        }
        if ($sessionStartUtc -gt [DateTime]::UtcNow.AddSeconds(10)) {
            $evidence.Reason = "future-session"
            return [pscustomobject]$evidence
        }

        $evidence.Valid = $true
        $evidence.Reason = "ok"
        $evidence.PluginVersion = [string]$payload.pluginVersion
        $evidence.SessionStartUtc = $sessionStartUtc.ToString("o")
        return [pscustomobject]$evidence
    }
    catch {
        $evidence.Reason = "invalid-json-or-dead-process: $($_.Exception.Message)"
        return [pscustomobject]$evidence
    }
}

function Test-McpEndpoint {
    param([string]$Uri, [int]$TimeoutSec)

    $result = [ordered]@{
        Uri = $Uri
        Ready = $false
        HttpStatus = $null
        SessionId = $null
        Error = $null
    }
    $body = @{
        jsonrpc = "2.0"
        id = 1
        method = "initialize"
        params = @{
            protocolVersion = "2025-11-25"
            capabilities = @{}
            clientInfo = @{ name = "operate-webtoue-editor"; version = "1.0" }
        }
    } | ConvertTo-Json -Depth 8 -Compress

    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri $Uri -Method Post -ContentType "application/json" -Headers @{ Accept = "application/json, text/event-stream" } -Body $body -TimeoutSec $TimeoutSec
        $result.HttpStatus = [int]$response.StatusCode
        $result.SessionId = [string]$response.Headers["Mcp-Session-Id"]
        $result.Ready = ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
    }
    catch {
        $result.Error = $_.Exception.Message
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            $result.HttpStatus = [int]$_.Exception.Response.StatusCode
        }
    }
    return [pscustomobject]$result
}

function Get-StatusResult {
    param([string]$Root, [string]$ProjectFile, [string]$Endpoint)

    $editors = @(Get-UnrealEditors)
    $editorEvidence = @()
    foreach ($editor in $editors) {
        $editorEvidence += [pscustomobject][ordered]@{
            Pid = $editor.Id
            Path = $editor.Path
            StartTimeUtc = $editor.StartTime.ToUniversalTime().ToString("o")
            ReadySignal = Get-ReadySignalEvidence -Root $Root -EditorPid $editor.Id
        }
    }
    $mcp = Test-McpEndpoint -Uri $Endpoint -TimeoutSec $McpTimeoutSec
    $healthy = ($editorEvidence.Count -eq 1 -and $editorEvidence[0].ReadySignal.Valid -and $mcp.Ready)
    return [pscustomobject][ordered]@{
        Action = "Status"
        Project = $ProjectFile
        Editors = $editorEvidence
        Mcp = $mcp
        Healthy = $healthy
    }
}

function Backup-EditorLogs {
    param([string]$Root)

    $logsPath = Join-Path $Root "Saved\Logs"
    if (-not (Test-Path -LiteralPath $logsPath)) {
        return $null
    }
    $stamp = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
    $archiveRoot = Join-Path $Root "Saved\VibeUE\LifecycleArchives\$stamp"
    $archiveLogs = Join-Path $archiveRoot "Logs"
    New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
    Copy-Item -LiteralPath $logsPath -Destination $archiveLogs -Recurse -Force
    return $archiveLogs
}

function Wait-VibeReadySignal {
    param([string]$Root, [int]$EditorPid, [int]$TimeoutSec)

    $fileName = "editor-$EditorPid-true.json"
    $first = Get-ReadySignalEvidence -Root $Root -EditorPid $EditorPid
    if ($first.Valid) {
        return $first
    }

    $watcher = New-Object System.IO.FileSystemWatcher
    $watcher.Path = $Root
    $watcher.Filter = $fileName
    $watcher.IncludeSubdirectories = $true
    $watcher.EnableRaisingEvents = $true
    $createdId = "WebToUE.Ready.Created.$EditorPid.$([Guid]::NewGuid())"
    $renamedId = "WebToUE.Ready.Renamed.$EditorPid.$([Guid]::NewGuid())"
    $createdSubscription = Register-ObjectEvent -InputObject $watcher -EventName Created -SourceIdentifier $createdId
    $renamedSubscription = Register-ObjectEvent -InputObject $watcher -EventName Renamed -SourceIdentifier $renamedId
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)

    try {
        while ([DateTime]::UtcNow -lt $deadline) {
            $process = Get-Process -Id $EditorPid -ErrorAction SilentlyContinue
            if (-not $process) {
                Stop-Lifecycle -Code 43 -Message "Editor PID $EditorPid exited before VibeUE readiness."
            }

            $remaining = [Math]::Max(1, [int][Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalSeconds))
            $waitSlice = [Math]::Min(5, $remaining)
            $null = Wait-Event -Timeout $waitSlice
            Get-Event -SourceIdentifier $createdId -ErrorAction SilentlyContinue | Remove-Event -ErrorAction SilentlyContinue
            Get-Event -SourceIdentifier $renamedId -ErrorAction SilentlyContinue | Remove-Event -ErrorAction SilentlyContinue

            $evidence = Get-ReadySignalEvidence -Root $Root -EditorPid $EditorPid
            if ($evidence.Valid) {
                return $evidence
            }
        }
        Stop-Lifecycle -Code 42 -Message "Editor PID $EditorPid did not produce a valid readiness signal within $TimeoutSec seconds."
    }
    finally {
        Unregister-Event -SourceIdentifier $createdId -ErrorAction SilentlyContinue
        Unregister-Event -SourceIdentifier $renamedId -ErrorAction SilentlyContinue
        Remove-Job -Id $createdSubscription.Id -Force -ErrorAction SilentlyContinue
        Remove-Job -Id $renamedSubscription.Id -Force -ErrorAction SilentlyContinue
        $watcher.Dispose()
    }
}

$resolvedRoot = Resolve-ProjectRoot -RequestedRoot $ProjectRoot
$uprojects = @(Get-ChildItem -LiteralPath $resolvedRoot -Filter "*.uproject" -File)
if ($uprojects.Count -ne 1) {
    Stop-Lifecycle -Code 2 -Message "Expected exactly one .uproject in '$resolvedRoot'; found $($uprojects.Count)."
}
$projectFile = $uprojects[0].FullName
$vibeScript = Join-Path $resolvedRoot "Plugins\VibeUE\BuildAndLaunchGame.ps1"
if (-not (Test-Path -LiteralPath $vibeScript)) {
    Stop-Lifecycle -Code 3 -Message "VibeUE launch script is missing: $vibeScript"
}

if ($Action -eq "Status") {
    $statusResult = Get-StatusResult -Root $resolvedRoot -ProjectFile $projectFile -Endpoint $McpUri
    $statusResult | ConvertTo-Json -Depth 10
    if ($statusResult.Healthy) { exit 0 }
    exit 10
}

$editorsBefore = @(Get-UnrealEditors)
if ($editorsBefore.Count -gt 1) {
    Stop-Lifecycle -Code 30 -Message "Found multiple Unreal Editors ($($editorsBefore.Id -join ', ')); refusing an ambiguous shutdown."
}

$previousPid = $null
if ($editorsBefore.Count -eq 1) {
    $editor = $editorsBefore[0]
    $previousPid = $editor.Id
    $signal = Get-ReadySignalEvidence -Root $resolvedRoot -EditorPid $editor.Id
    if (-not $signal.Valid) {
        Stop-Lifecycle -Code 31 -Message "Editor PID $($editor.Id) is not proven to belong to this VibeUE project ($($signal.Reason))."
    }
    if (-not $AssetsSaved) {
        Stop-Lifecycle -Code 32 -Message "An Editor is running. Stop PIE, save intended assets through MCP, then rerun with -AssetsSaved."
    }
}

$archivePath = Backup-EditorLogs -Root $resolvedRoot
if ($archivePath) {
    Write-Host "Archived current Editor logs: $archivePath" -ForegroundColor Cyan
}

if ($editorsBefore.Count -eq 1) {
    Write-Host "Requesting graceful shutdown of Editor PID $previousPid..." -ForegroundColor Yellow
    $closeRequested = $editorsBefore[0].CloseMainWindow()
    if (-not $closeRequested) {
        Stop-Lifecycle -Code 33 -Message "Editor PID $previousPid did not accept a normal close request."
    }
    if (-not $editorsBefore[0].WaitForExit($GracefulCloseTimeoutSec * 1000)) {
        Stop-Lifecycle -Code 34 -Message "Editor PID $previousPid remains alive after $GracefulCloseTimeoutSec seconds. Resolve its save/modal UI; it was not force-killed."
    }
}

if (@(Get-UnrealEditors).Count -ne 0) {
    Stop-Lifecycle -Code 35 -Message "An Unreal Editor appeared or remained after the safety gate; VibeUE was not invoked."
}

$vibeParameters = @{}
if ($StrictRebuild) { $vibeParameters.StrictRebuild = $true }
if ($Clean) { $vibeParameters.Clean = $true }
if ($SkipBuild) { $vibeParameters.SkipBuild = $true }

Write-Host "Invoking VibeUE's supported build-and-launch script..." -ForegroundColor Cyan
$vibeOutput = @()
& $vibeScript @vibeParameters | Tee-Object -Variable vibeOutput

$pidLines = @($vibeOutput | ForEach-Object { $_.ToString() } | Where-Object { $_ -match '^Editor-PID=(\d+)$' })
if ($pidLines.Count -ne 1) {
    Stop-Lifecycle -Code 40 -Message "Could not parse exactly one Editor-PID from VibeUE output."
}
$newPid = [int]([regex]::Match($pidLines[0], '^Editor-PID=(\d+)$').Groups[1].Value)
$ready = Wait-VibeReadySignal -Root $resolvedRoot -EditorPid $newPid -TimeoutSec $ReadyTimeoutSec
$mcp = Test-McpEndpoint -Uri $McpUri -TimeoutSec $McpTimeoutSec
$healthy = ($ready.Valid -and $mcp.Ready)

[pscustomobject][ordered]@{
    Action = "SafeBuildAndLaunch"
    Project = $projectFile
    PreviousEditorPid = $previousPid
    NewEditorPid = $newPid
    ArchivedLogs = $archivePath
    BuildOptions = [ordered]@{
        StrictRebuild = [bool]$StrictRebuild
        Clean = [bool]$Clean
        SkipBuild = [bool]$SkipBuild
    }
    ReadySignal = $ready
    Mcp = $mcp
    Healthy = $healthy
} | ConvertTo-Json -Depth 10

if (-not $healthy) {
    Stop-Lifecycle -Code 44 -Message "The new Editor signaled readiness, but MCP initialization did not succeed."
}
exit 0
