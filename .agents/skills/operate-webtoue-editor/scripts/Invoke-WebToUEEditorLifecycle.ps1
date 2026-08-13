[CmdletBinding()]
param(
    [ValidateSet("Status", "Preflight", "SafeBuildAndLaunch", "SafeBuildCookAndLaunch")]
    [string]$Action = "Status",
    [string]$ProjectRoot,
    [string]$EngineRoot,
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
    [int]$McpTimeoutSec = 10,
    [ValidateRange(0, 30)]
    [int]$McpRetryDelaySec = 2,
    [ValidateSet("Win64")]
    [string]$TargetPlatform = "Win64",
    [ValidateSet("Development", "Shipping")]
    [string]$ClientConfig = "Development",
    [ValidateRange(1024, 65535)]
    [int]$CookerMcpPort = 8001
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$script:OperationState = $null
$script:OperationStatePath = $null

function Write-OperationState {
    param([System.Collections.IDictionary]$State, [string]$Path)

    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $State.UpdatedUtc = [DateTime]::UtcNow.ToString("o")
        [System.IO.File]::WriteAllText(
            $temporaryPath,
            ($State | ConvertTo-Json -Depth 10),
            [System.Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Read-OperationState {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    try {
        return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    }
    catch {
        return [pscustomobject][ordered]@{
            Phase = "Invalid"
            Active = $false
            Error = $_.Exception.Message
        }
    }
}

function Set-OperationPhase {
    param([string]$Phase, [string]$ErrorMessage = $null)

    if (-not $script:OperationState -or -not $script:OperationStatePath) {
        return
    }
    $script:OperationState.Phase = $Phase
    $script:OperationState.Error = $ErrorMessage
    if ($Phase -in @("Healthy", "EditorReadyMcpPending", "Failed")) {
        $script:OperationState.CompletedUtc = [DateTime]::UtcNow.ToString("o")
    }
    Write-OperationState -State $script:OperationState -Path $script:OperationStatePath
}

function Stop-Lifecycle {
    param([int]$Code, [string]$Message)
    Set-OperationPhase -Phase "Failed" -ErrorMessage $Message
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

function Resolve-EngineInstall {
    param([string]$RequestedRoot, [string]$ProjectFile, [System.Array]$Editors)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidates += $RequestedRoot
    }

    foreach ($editor in @($Editors)) {
        if ($editor.Path -and $editor.Path -match '^(.*)\\Engine\\Binaries\\Win64\\UnrealEditor[^\\]*\.exe$') {
            $candidates += $Matches[1]
        }
    }

    try {
        $association = [string]((Get-Content -Raw -LiteralPath $ProjectFile | ConvertFrom-Json).EngineAssociation)
        if (-not [string]::IsNullOrWhiteSpace($association)) {
            $candidates += @(
                "D:\UE\UE_$association",
                "E:\UE\UE_$association",
                "C:\UE\UE_$association",
                "D:\Program Files\Epic Games\UE_$association",
                "E:\Program Files\Epic Games\UE_$association",
                "C:\Program Files\Epic Games\UE_$association"
            )
        }
    }
    catch {}

    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($candidate) -or -not (Test-Path -LiteralPath $candidate)) {
            continue
        }
        $resolved = (Resolve-Path -LiteralPath $candidate).Path
        $buildBat = Join-Path $resolved "Engine\Build\BatchFiles\Build.bat"
        $editorExe = Join-Path $resolved "Engine\Binaries\Win64\UnrealEditor.exe"
        if ((Test-Path -LiteralPath $buildBat) -and (Test-Path -LiteralPath $editorExe)) {
            return $resolved
        }
    }
    return $null
}

function Test-WriteProbe {
    param([string]$Name, [string]$Directory)

    $result = [ordered]@{
        Name = $Name
        Directory = $Directory
        Writable = $false
        Error = $null
    }
    $probePath = $null
    try {
        if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
            throw "Directory does not exist."
        }
        $probePath = Join-Path $Directory (".webtoue-write-probe-{0}.tmp" -f [Guid]::NewGuid().ToString("N"))
        [System.IO.File]::WriteAllText($probePath, "WebToUE lifecycle write probe", [System.Text.UTF8Encoding]::new($false))
        Remove-Item -LiteralPath $probePath -Force
        $probePath = $null
        $result.Writable = $true
    }
    catch {
        $result.Error = $_.Exception.Message
    }
    finally {
        if ($probePath -and (Test-Path -LiteralPath $probePath)) {
            Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue
        }
    }
    return [pscustomobject]$result
}

function Get-PreflightResult {
    param([string]$Root, [string]$ResolvedEngineRoot)

    $probeDirectories = [ordered]@{
        ProjectSaved = (Join-Path $Root "Saved")
        ProjectIntermediate = (Join-Path $Root "Intermediate")
        EngineIntermediate = (Join-Path $ResolvedEngineRoot "Engine\Intermediate")
        UnrealBuildToolLocalData = (Join-Path $env:LOCALAPPDATA "UnrealBuildTool")
        UnrealEngineLocalData = (Join-Path $env:LOCALAPPDATA "UnrealEngine")
    }
    $probes = @()
    foreach ($entry in $probeDirectories.GetEnumerator()) {
        $probes += Test-WriteProbe -Name $entry.Key -Directory $entry.Value
    }
    $failed = @($probes | Where-Object { -not $_.Writable })
    return [pscustomobject][ordered]@{
        EngineRoot = $ResolvedEngineRoot
        Probes = $probes
        Passed = ($failed.Count -eq 0)
        FailedCount = $failed.Count
    }
}

function Get-ActiveOperationPids {
    param([object]$Operation)

    if (-not $Operation -or $Operation.Phase -in @("Healthy", "EditorReadyMcpPending", "Failed", "Invalid")) {
        return @()
    }
    $activePids = @()
    foreach ($propertyName in @("OwnerPid", "ReleaseHostPid", "VibeProcessPid", "NewEditorPid")) {
        $property = $Operation.PSObject.Properties[$propertyName]
        if ($property -and $property.Value -and (Get-Process -Id ([int]$property.Value) -ErrorAction SilentlyContinue)) {
            $activePids += [int]$property.Value
        }
    }
    $releaseProcessProperty = $Operation.PSObject.Properties["ReleaseProcessPids"]
    if ($releaseProcessProperty) {
        foreach ($processId in @($releaseProcessProperty.Value)) {
            if ($processId -and (Get-Process -Id ([int]$processId) -ErrorAction SilentlyContinue)) {
                $activePids += [int]$processId
            }
        }
    }
    return @($activePids | Select-Object -Unique)
}

function Get-DescendantProcessIds {
    param([int]$RootPid)

    $processes = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Select-Object ProcessId, ParentProcessId)
    $knownParents = [System.Collections.Generic.HashSet[int]]::new()
    $descendants = [System.Collections.Generic.HashSet[int]]::new()
    $null = $knownParents.Add($RootPid)
    do {
        $added = $false
        foreach ($process in $processes) {
            $processId = [int]$process.ProcessId
            if ($knownParents.Contains([int]$process.ParentProcessId) -and $processId -ne $RootPid -and $descendants.Add($processId)) {
                $null = $knownParents.Add($processId)
                $added = $true
            }
        }
    } while ($added)
    return @($descendants)
}

function Remove-StaleLifecycleScripts {
    param([string]$VibeScriptPath)

    $removed = @()
    $staleScripts = @(Get-ChildItem -LiteralPath (Split-Path -Parent $VibeScriptPath) `
        -Filter ".BuildAndLaunchGame.WebToUE.*.ps1" -File -Force -ErrorAction SilentlyContinue)
    foreach ($staleScript in $staleScripts) {
        Remove-Item -LiteralPath $staleScript.FullName -Force
        $removed += $staleScript.FullName
    }
    return $removed
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

function Test-McpEndpointWithRetry {
    param([string]$Uri, [int]$TimeoutSec, [int]$RetryDelaySec)

    $first = Test-McpEndpoint -Uri $Uri -TimeoutSec $TimeoutSec
    if ($first.Ready -or $RetryDelaySec -le 0) {
        $first | Add-Member -NotePropertyName AttemptCount -NotePropertyValue 1 -Force
        return $first
    }

    Start-Sleep -Seconds $RetryDelaySec
    $second = Test-McpEndpoint -Uri $Uri -TimeoutSec $TimeoutSec
    $second | Add-Member -NotePropertyName AttemptCount -NotePropertyValue 2 -Force
    return $second
}

function Get-BuildCookRunArguments {
    param([string]$ProjectFile, [string]$Platform, [string]$Configuration, [int]$McpPort)

    return @(
        "BuildCookRun",
        "-Project=$ProjectFile",
        "-NoP4",
        "-Platform=$Platform",
        "-ClientConfig=$Configuration",
        "-Build",
        "-Cook",
        "-Stage",
        "-Pak",
        "-IoStore",
        "-UTF8Output",
        "-Unattended",
        "-AdditionalCookerOptions=-ModelContextProtocolPort=$McpPort"
    )
}

function ConvertTo-PowerShellSingleQuotedLiteral {
    param([string]$Value)

    return "'" + $Value.Replace("'", "''") + "'"
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
        if ($createdSubscription) {
            Remove-Job -Id $createdSubscription.Id -Force -ErrorAction SilentlyContinue
        }
        if ($renamedSubscription) {
            Remove-Job -Id $renamedSubscription.Id -Force -ErrorAction SilentlyContinue
        }
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

$operationStatePath = Join-Path $resolvedRoot "Saved\VibeUE\Lifecycle\operation.json"

if ($Action -eq "Status") {
    $statusResult = Get-StatusResult -Root $resolvedRoot -ProjectFile $projectFile -Endpoint $McpUri
    $operationStatus = Read-OperationState -Path $operationStatePath
    $statusResult | Add-Member -NotePropertyName Operation -NotePropertyValue $operationStatus
    $statusResult | ConvertTo-Json -Depth 10
    if ($statusResult.Healthy) { exit 0 }
    exit 10
}

$editorsBefore = @(Get-UnrealEditors)
$resolvedEngineRoot = Resolve-EngineInstall -RequestedRoot $EngineRoot -ProjectFile $projectFile -Editors $editorsBefore
if (-not $resolvedEngineRoot) {
    Stop-Lifecycle -Code 4 -Message "Could not resolve a usable Unreal Engine install. Pass -EngineRoot explicitly."
}

$preflight = Get-PreflightResult -Root $resolvedRoot -ResolvedEngineRoot $resolvedEngineRoot
if ($Action -eq "Preflight") {
    $preflight | ConvertTo-Json -Depth 10
    if ($preflight.Passed) { exit 0 }
    exit 5
}

if (-not $preflight.Passed) {
    $preflight | ConvertTo-Json -Depth 10
    Stop-Lifecycle -Code 5 -Message "Lifecycle write-access preflight failed before Editor shutdown. Run this exact lifecycle command outside the workspace-only sandbox."
}

$isReleaseAction = ($Action -eq "SafeBuildCookAndLaunch")
$runUatScript = Join-Path $resolvedEngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
if ($isReleaseAction -and -not (Test-Path -LiteralPath $runUatScript -PathType Leaf)) {
    Stop-Lifecycle -Code 6 -Message "RunUAT is missing: $runUatScript"
}
if ($isReleaseAction -and ($StrictRebuild -or $Clean -or $SkipBuild)) {
    Stop-Lifecycle -Code 7 -Message "SafeBuildCookAndLaunch owns its BuildCookRun and relaunch phases; do not combine it with -StrictRebuild, -Clean, or -SkipBuild."
}

$mutexName = "Local\WebToUE.EditorLifecycle.$($uprojects[0].BaseName)"
$lifecycleMutex = [System.Threading.Mutex]::new($false, $mutexName)
$mutexAcquired = $false
try {
    $mutexAcquired = $lifecycleMutex.WaitOne(0)
}
catch [System.Threading.AbandonedMutexException] {
    $mutexAcquired = $true
}
if (-not $mutexAcquired) {
    $activeOperation = Read-OperationState -Path $operationStatePath
    if ($activeOperation) {
        $activeOperation | ConvertTo-Json -Depth 10
    }
    Stop-Lifecycle -Code 20 -Message "Another lifecycle operation owns '$mutexName'. Observe that operation; do not start a duplicate UBT or Editor launch."
}

$existingOperation = Read-OperationState -Path $operationStatePath
if ($existingOperation -and $existingOperation.Phase -notin @("Healthy", "Failed", "Invalid")) {
    $activePids = @(Get-ActiveOperationPids -Operation $existingOperation)
    if ($activePids.Count -gt 0) {
        $existingOperation | ConvertTo-Json -Depth 10
        Stop-Lifecycle -Code 20 -Message "Lifecycle operation '$($existingOperation.OperationId)' still has active PID(s) $($activePids -join ', '). Observe it; do not start a duplicate UBT or Editor launch."
    }
}

foreach ($removedScript in @(Remove-StaleLifecycleScripts -VibeScriptPath $vibeScript)) {
    Write-Host "Removed stale WebToUE lifecycle script: $removedScript" -ForegroundColor Yellow
}

$operationId = [Guid]::NewGuid().ToString("N")
$operationDirectory = Join-Path $resolvedRoot "Saved\VibeUE\Lifecycle\Operations\$operationId"
$script:OperationStatePath = $operationStatePath
$script:OperationState = [ordered]@{
    OperationId = $operationId
    Action = $Action
    OwnerPid = $PID
    Phase = "PreflightPassed"
    Project = $projectFile
    EngineRoot = $resolvedEngineRoot
    StartedUtc = [DateTime]::UtcNow.ToString("o")
    UpdatedUtc = $null
    CompletedUtc = $null
    PreviousEditorPid = $null
    ReleaseHostPid = $null
    ReleaseProcessPids = @()
    AutomationToolExitCode = $null
    VibeProcessPid = $null
    NewEditorPid = $null
    EditorReady = $false
    McpReady = $false
    ReleaseInvocationPath = $null
    ReleaseOutputPath = (Join-Path $operationDirectory "uat-stdout.log")
    ReleaseErrorPath = (Join-Path $operationDirectory "uat-stderr.log")
    OutputPath = (Join-Path $operationDirectory "vibe-stdout.log")
    ErrorPath = (Join-Path $operationDirectory "vibe-stderr.log")
    TemporaryVibeScript = $null
    Error = $null
}
Write-OperationState -State $script:OperationState -Path $script:OperationStatePath

if ($editorsBefore.Count -gt 1) {
    Stop-Lifecycle -Code 30 -Message "Found multiple Unreal Editors ($($editorsBefore.Id -join ', ')); refusing an ambiguous shutdown."
}

$previousPid = $null
if ($editorsBefore.Count -eq 1) {
    $editor = $editorsBefore[0]
    $previousPid = $editor.Id
    $script:OperationState.PreviousEditorPid = $previousPid
    Write-OperationState -State $script:OperationState -Path $script:OperationStatePath
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
    Set-OperationPhase -Phase "ClosingEditor"
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

$releaseResult = $null
if ($isReleaseAction) {
    New-Item -ItemType Directory -Path $operationDirectory -Force | Out-Null
    $releaseArguments = Get-BuildCookRunArguments `
        -ProjectFile $projectFile `
        -Platform $TargetPlatform `
        -Configuration $ClientConfig `
        -McpPort $CookerMcpPort
    $releaseInvocationPath = Join-Path $operationDirectory "Invoke-BuildCookRun.ps1"
    $argumentLiterals = @($releaseArguments | ForEach-Object { "    " + (ConvertTo-PowerShellSingleQuotedLiteral -Value $_) })
    $releaseInvocation = @(
        '$ErrorActionPreference = "Stop"',
        '$arguments = @(',
        ($argumentLiterals -join ",`r`n"),
        ')',
        "& $(ConvertTo-PowerShellSingleQuotedLiteral -Value $runUatScript) @arguments",
        'exit $LASTEXITCODE'
    ) -join "`r`n"
    [System.IO.File]::WriteAllText($releaseInvocationPath, $releaseInvocation, [System.Text.UTF8Encoding]::new($false))
    $script:OperationState.ReleaseInvocationPath = $releaseInvocationPath
    Set-OperationPhase -Phase "RunningBuildCookRun"

    Write-Host "Running tracked BuildCookRun ($TargetPlatform $ClientConfig, Cook/Stage/Pak/IoStore)..." -ForegroundColor Cyan
    $releaseProcess = Start-Process `
        -FilePath (Join-Path $PSHOME "powershell.exe") `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $releaseInvocationPath) `
        -NoNewWindow `
        -PassThru `
        -RedirectStandardOutput $script:OperationState.ReleaseOutputPath `
        -RedirectStandardError $script:OperationState.ReleaseErrorPath
    $script:OperationState.ReleaseHostPid = $releaseProcess.Id
    Write-OperationState -State $script:OperationState -Path $script:OperationStatePath
    while (-not $releaseProcess.WaitForExit(2000)) {
        $script:OperationState.ReleaseProcessPids = @(
            @($releaseProcess.Id) + @(Get-DescendantProcessIds -RootPid $releaseProcess.Id) |
                Select-Object -Unique)
        Write-OperationState -State $script:OperationState -Path $script:OperationStatePath
    }
    $releaseProcess.Refresh()
    $releaseExitCode = [int]$releaseProcess.ExitCode
    $script:OperationState.AutomationToolExitCode = $releaseExitCode
    Write-OperationState -State $script:OperationState -Path $script:OperationStatePath

    $releaseOutput = @(Get-Content -LiteralPath $script:OperationState.ReleaseOutputPath -ErrorAction SilentlyContinue)
    $releaseErrors = @(Get-Content -LiteralPath $script:OperationState.ReleaseErrorPath -ErrorAction SilentlyContinue)
    Write-Host "BuildCookRun output: $($script:OperationState.ReleaseOutputPath)" -ForegroundColor Cyan
    $releaseOutput | Select-Object -Last 80 | ForEach-Object { Write-Host $_ }
    $releaseErrors | Select-Object -Last 80 | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    if ($releaseExitCode -ne 0) {
        Stop-Lifecycle -Code 38 -Message "BuildCookRun failed with exit code $releaseExitCode. Inspect the persisted UAT logs before retrying."
    }

    $releaseResult = [ordered]@{
        Platform = $TargetPlatform
        ClientConfig = $ClientConfig
        CookerMcpPort = $CookerMcpPort
        ReleaseHostPid = $releaseProcess.Id
        ReleaseProcessPids = @($script:OperationState.ReleaseProcessPids)
        ExitCode = $releaseExitCode
        OutputPath = $script:OperationState.ReleaseOutputPath
        ErrorPath = $script:OperationState.ReleaseErrorPath
    }
    Set-OperationPhase -Phase "BuildCookRunSucceeded"
}

$vibeScriptToRun = $vibeScript
$temporaryVibeScript = $null
if ($resolvedEngineRoot) {
    $vibeSource = [System.IO.File]::ReadAllText($vibeScript)
    $enginePathMarker = '$enginePath = $null'
    $markerCount = ([regex]::Matches($vibeSource, [regex]::Escape($enginePathMarker))).Count
    if ($markerCount -ne 1) {
        Stop-Lifecycle -Code 36 -Message "Expected exactly one VibeUE engine-path marker; found $markerCount."
    }

    $escapedEngineRoot = $resolvedEngineRoot.Replace("'", "''")
    $temporaryVibeScript = Join-Path (Split-Path -Parent $vibeScript) (".BuildAndLaunchGame.WebToUE.{0}.ps1" -f [Guid]::NewGuid().ToString("N"))
    $patchedSource = $vibeSource.Replace($enginePathMarker, "`$enginePath = '$escapedEngineRoot'")
    [System.IO.File]::WriteAllText($temporaryVibeScript, $patchedSource, [System.Text.UTF8Encoding]::new($false))
    $vibeScriptToRun = $temporaryVibeScript
    $script:OperationState.TemporaryVibeScript = $temporaryVibeScript
    Write-OperationState -State $script:OperationState -Path $script:OperationStatePath
}

$vibeArguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $vibeScriptToRun)
if ($StrictRebuild) { $vibeArguments += "-StrictRebuild" }
if ($Clean) { $vibeArguments += "-Clean" }
if ($SkipBuild -or $isReleaseAction) { $vibeArguments += "-SkipBuild" }

Write-Host "Invoking VibeUE's supported build-and-launch script..." -ForegroundColor Cyan
$vibeOutput = @()
try {
    New-Item -ItemType Directory -Path $operationDirectory -Force | Out-Null
    Set-OperationPhase -Phase "InvokingVibeUE"
    $vibeProcess = Start-Process `
        -FilePath (Join-Path $PSHOME "powershell.exe") `
        -ArgumentList $vibeArguments `
        -NoNewWindow `
        -PassThru `
        -RedirectStandardOutput $script:OperationState.OutputPath `
        -RedirectStandardError $script:OperationState.ErrorPath
    $script:OperationState.VibeProcessPid = $vibeProcess.Id
    Write-OperationState -State $script:OperationState -Path $script:OperationStatePath
    $vibeProcess.WaitForExit()
    $vibeProcess.Refresh()
    $vibeExitCode = [int]$vibeProcess.ExitCode
    $vibeOutput = @(Get-Content -LiteralPath $script:OperationState.OutputPath -ErrorAction SilentlyContinue)
    $vibeErrors = @(Get-Content -LiteralPath $script:OperationState.ErrorPath -ErrorAction SilentlyContinue)
    $vibeOutput | ForEach-Object { Write-Host $_ }
    $vibeErrors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
}
finally {
    if ($temporaryVibeScript -and (Test-Path -LiteralPath $temporaryVibeScript)) {
        Remove-Item -LiteralPath $temporaryVibeScript -Force
    }
}
if ($vibeExitCode -ne 0) {
    Stop-Lifecycle -Code 39 -Message "VibeUE build-and-launch script failed with exit code $vibeExitCode."
}

$pidLines = @($vibeOutput | ForEach-Object { $_.ToString() } | Where-Object { $_ -match '^Editor-PID=(\d+)$' })
if ($pidLines.Count -ne 1) {
    Stop-Lifecycle -Code 40 -Message "Could not parse exactly one Editor-PID from VibeUE output."
}
$newPid = [int]([regex]::Match($pidLines[0], '^Editor-PID=(\d+)$').Groups[1].Value)
$script:OperationState.NewEditorPid = $newPid
Set-OperationPhase -Phase "WaitingReadiness"
$ready = Wait-VibeReadySignal -Root $resolvedRoot -EditorPid $newPid -TimeoutSec $ReadyTimeoutSec
$script:OperationState.EditorReady = [bool]$ready.Valid
Set-OperationPhase -Phase "CheckingMcp"
$mcp = Test-McpEndpointWithRetry -Uri $McpUri -TimeoutSec $McpTimeoutSec -RetryDelaySec $McpRetryDelaySec
$script:OperationState.McpReady = [bool]$mcp.Ready
$healthy = ($ready.Valid -and $mcp.Ready)
$script:OperationState.TemporaryVibeScript = $null
if ($healthy) {
    Set-OperationPhase -Phase "Healthy"
}
else {
    Set-OperationPhase -Phase "EditorReadyMcpPending" -ErrorMessage "Editor readiness is valid, but MCP initialization did not succeed after the bounded recheck."
}

[pscustomobject][ordered]@{
    Action = $Action
    Project = $projectFile
    PreviousEditorPid = $previousPid
    NewEditorPid = $newPid
    ArchivedLogs = $archivePath
    BuildOptions = [ordered]@{
        EngineRoot = $resolvedEngineRoot
        StrictRebuild = [bool]$StrictRebuild
        Clean = [bool]$Clean
        SkipBuild = [bool]($SkipBuild -or $isReleaseAction)
    }
    Release = $releaseResult
    ReadySignal = $ready
    Mcp = $mcp
    Healthy = $healthy
} | ConvertTo-Json -Depth 10

$lifecycleMutex.ReleaseMutex()
$lifecycleMutex.Dispose()
if (-not $healthy) {
    Write-Host "ERROR: The new Editor signaled readiness, but MCP initialization did not succeed after $($mcp.AttemptCount) attempt(s)." -ForegroundColor Red
    exit 44
}
exit 0
