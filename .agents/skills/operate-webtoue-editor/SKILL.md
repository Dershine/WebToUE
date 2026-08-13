---
name: operate-webtoue-editor
description: Safely operate the WebToUE Unreal Engine 5.8 Editor with VibeUE and MCP. Use for editor health checks, PIE control, saving assets, rebuilding, tracked BuildCookRun release gates, closing or relaunching the Editor, waiting for VibeUE readiness, validating MCP, capturing in-editor evidence, or running Unreal automation tests in this repository. Do not use for source-only edits that require no Editor interaction.
---

# Operate the WebToUE Editor

Run a guarded Editor workflow that preserves user work and proves the new Editor session is usable. Treat source compilation, VibeUE readiness, MCP reachability, Python readiness, and test results as separate gates.

## Authority and evidence boundary

This skill owns **how** to operate the Editor, lifecycle wrapper, MCP, Automation, visual capture, and tracked release gates safely. It does not select roadmap scope or define product performance budgets; those are owned by `Plugins/WebToUE/Docs/WTUE_TechnicalSummary.md`.

Keep these conclusions separate:

- `SafeBuildAndLaunch` can prove the requested source build, Editor relaunch, readiness, and MCP gates recorded by the lifecycle operation.
- `SafeBuildCookAndLaunch` can additionally prove its recorded Build/Cook/Stage/Pak/IoStore result. It does not launch or exercise the staged game and therefore cannot alone prove Packaged Runtime correctness.
- Editor `PerformanceService`, Automation, and Paint microbenchmarks are diagnostic or regression evidence within their measured workload. They do not alone prove input-to-pixel latency, Render Thread/GPU cost, first-frame behavior, process/VRAM budgets, or product-level equivalence with UMG/Gameface.
- When the task requires evidence outside the available workflow, report the missing runtime harness or measurement as unverified. Do not upgrade a nearby gate into stronger evidence.

## Start with status

Run the non-mutating status probe from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\operate-webtoue-editor\scripts\Invoke-WebToUEEditorLifecycle.ps1 -Action Status
```

Read the JSON result. Trust an Editor process only when its PID matches a valid readiness file under `Saved/VibeUE/Signals`. Do not close an unrelated or ambiguous Editor process.

`Status` also returns the latest operation record from `Saved/VibeUE/Lifecycle/operation.json`. If it names a live owner, release host/process-tree, VibeUE, or Editor PID in a nonterminal phase, observe that operation; never start a second lifecycle command.

## Choose the workflow

- For an Editor-only inspection or asset task, keep the current Editor open and use MCP.
- For PIE, viewport, asset, Blueprint, material, Python, log, or automation-test work, discover the relevant VibeUE/Epic toolset before calling it.
- For C++ or plugin changes that require a fresh process, follow the safe rebuild workflow below.
- For a final Win64 Cook/Stage/Pak/IoStore gate after source freeze, use the tracked release workflow below.
- For a stopped Editor, run the safe rebuild workflow without `-AssetsSaved`; no close step is needed.
- Never force-kill `UnrealEditor.exe`. Stop and report a save dialog or shutdown timeout.

## Use MCP safely

1. Discover exact toolset names and schemas; do not guess signatures from prose.
2. Prefer one batched `execute_python_code` call for multi-step Editor Python work. Start it with `import unreal` and print concise evidence.
3. Use `unreal.ToolsetRegistry.execute_tool(...)` for Epic toolset calls that must be made from Python.
4. Before a rebuild, call `EditorToolset.EditorAppToolset.StopPIE` when PIE is running, save every intended dirty asset/package, and re-query state to prove no intended changes remain dirty.
5. Preserve relevant viewport, log, compile, or test evidence before shutdown. The lifecycle script archives `Saved/Logs` before VibeUE clears the active logs.
6. Do not read or modify `.uasset` files directly on disk.

## Rebuild and relaunch

UBT, Turnkey, UBA, Zen, and the Editor write outside the repository. In Codex, run the lifecycle command through a narrowly scoped unsandboxed approval for this script; do not disable sandboxing globally and do not invoke arbitrary PowerShell with broad approval. The Editor must be launched from the same unsandboxed boundary so its child processes inherit usable permissions.

Before stopping a healthy Editor, run the exact write-access preflight:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\operate-webtoue-editor\scripts\Invoke-WebToUEEditorLifecycle.ps1 -Action Preflight -EngineRoot D:\UE\UE_5.8
```

All probes must report `Writable: true`. The safe build command repeats this preflight before it archives logs or requests Editor shutdown.

After MCP has stopped PIE and saved the intended work, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\operate-webtoue-editor\scripts\Invoke-WebToUEEditorLifecycle.ps1 -Action SafeBuildAndLaunch -AssetsSaved
```

If the vendored VibeUE script cannot discover a nonstandard engine install, pass a verified root such as `-EngineRoot D:\UE\UE_5.8`. The wrapper injects it into an ephemeral same-directory copy so the third-party script remains unchanged, then removes the copy in `finally`.

The script performs these guarded steps:

1. Resolve and validate the engine install.
2. Probe all required project, engine, and user-local write locations before Editor shutdown.
3. Acquire a project-scoped mutex and reject a duplicate operation or surviving owner/VibeUE/Editor PID.
4. Persist the operation id, phase, owner/release-host/release-process-tree/VibeUE/Editor PIDs, output paths, readiness flags, and terminal status under `Saved/VibeUE/Lifecycle`.
5. Require exactly one running Unreal Editor at most and prove its readiness belongs to this project.
6. Require `-AssetsSaved` when an Editor is running.
7. Archive `Saved/Logs`, request a normal window close, and stop if it does not exit within 60 seconds.
8. Invoke `Plugins/VibeUE/BuildAndLaunchGame.ps1` in an isolated child PowerShell process and capture its output without modifying the vendored file.
9. Parse the new `Editor-PID`, wait on filesystem events for its readiness file, validate PID/session time, then make one MCP initialization request and at most one delayed recheck.

Pass optional build switches only when the request calls for them:

- `-StrictRebuild`: fully recompile VibeUE under warnings-as-errors.
- `-Clean`: remove project/plugin build artifacts before compiling; this is destructive and requires explicit user authorization.
- `-SkipBuild`: relaunch without compiling.

Do not run VibeUE's build script directly while an Editor is open; it force-terminates processes after its own timeout.

Builds are long-running. If the command yields an execution cell, call `wait` on that same cell until it completes. Do not impose a short shell timeout and do not rerun the lifecycle command because output is temporarily quiet. A second invocation is a diagnostic error, not a continuation mechanism.

## Run the final BuildCookRun gate

Do not start an expensive release gate while source, tests, assets, or required documentation are still changing. First freeze the candidate: finish the scoped edits, pass the nearest focused tests, complete the required Editor compile/reload acceptance, and review the diff. A release-gate failure may reopen the candidate; after any corrective edit, repeat the affected cheap gates before rerunning the release action.

When Cook/Stage/Pak/IoStore evidence is required for the current change, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\operate-webtoue-editor\scripts\Invoke-WebToUEEditorLifecycle.ps1 -Action SafeBuildCookAndLaunch -AssetsSaved -EngineRoot D:\UE\UE_5.8
```

`SafeBuildCookAndLaunch` reuses the same preflight, mutex, project-ownership, asset-save, log-archive, and graceful-close safety gates. It then:

1. Starts `RunUAT.bat BuildCookRun` for Win64 with Build, Cook, Stage, Pak, and IoStore enabled.
2. Passes cooker MCP port `8001` so stale or separately managed port `8000` ownership cannot poison Cook.
3. Persists the BuildCookRun host and observed RunUAT/UBT/Commandlet process-tree PIDs, exit code, invocation file, and complete stdout/stderr log paths while waiting.
4. On success, invokes VibeUE with `-SkipBuild` to restore the Editor and runs the normal readiness/MCP gates.
5. On failure, leaves the UAT evidence intact and does not launch a second BuildCookRun. The Editor may remain closed; report that explicitly.

Use `-ClientConfig Shipping` only when Shipping is part of the requested release boundary. Do not combine this action with `-StrictRebuild`, `-Clean`, or `-SkipBuild`; the release action owns its build and relaunch phases. `BuildPlugin` remains a separate distribution gate and is required only when the task changes plugin packaging/distribution or explicitly requests it.

## Verify the new session

After the script reports `Healthy: true`:

1. Make one small `execute_python_code` probe and print the engine version plus project name.
2. Verify world readiness separately if the task needs a loaded level or actors.
3. Re-open or re-read each modified asset and check compile/status evidence.
4. Run focused automation tests through `AutomationTestToolset.DiscoverTests`, then `RunTests` or `RunTestsByFilter`. Do not rely on interactive `Automation RunTests`, which can remain queued in UE 5.8.
5. Capture a viewport image when visual correctness matters, inspect it, and iterate if needed.
6. Report PIDs, readiness signal, MCP result, build result, test counts/failures, and any archived-log path.

Read [references/verification.md](references/verification.md) when running the full post-launch validation or diagnosing a failed gate.

## Stop conditions

- Stop when more than one Editor process exists or the project/PID cannot be proven.
- Stop when a save or modal dialog prevents graceful shutdown.
- Stop when any preflight probe is not writable; the healthy Editor must remain open in this case.
- Stop when the operation mutex or persisted state identifies a live lifecycle PID, including the release host/process tree; observe the existing operation and its log paths instead of retrying.
- Stop when the new Editor exits, readiness exceeds 180 seconds, or the signal is stale/invalid.
- If readiness is valid but the bounded MCP recheck fails, preserve the terminal `EditorReadyMcpPending` state and diagnose MCP separately. Do not rebuild merely to repeat MCP initialization.
- Retry an unchanged failing operation at most twice, then report the evidence and blocker.
- Keep the Editor open at handoff unless the user explicitly asks to close it.
