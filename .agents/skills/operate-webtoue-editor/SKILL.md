---
name: operate-webtoue-editor
description: Safely operate the WebToUE Unreal Engine 5.8 Editor with VibeUE and MCP. Use for editor health checks, PIE control, saving assets, rebuilding, closing or relaunching the Editor, waiting for VibeUE readiness, validating MCP, capturing in-editor evidence, or running Unreal automation tests in this repository. Do not use for source-only edits that require no Editor interaction.
---

# Operate the WebToUE Editor

Run a guarded Editor workflow that preserves user work and proves the new Editor session is usable. Treat source compilation, VibeUE readiness, MCP reachability, Python readiness, and test results as separate gates.

## Start with status

Run the non-mutating status probe from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\operate-webtoue-editor\scripts\Invoke-WebToUEEditorLifecycle.ps1 -Action Status
```

Read the JSON result. Trust an Editor process only when its PID matches a valid readiness file under `Saved/VibeUE/Signals`. Do not close an unrelated or ambiguous Editor process.

## Choose the workflow

- For an Editor-only inspection or asset task, keep the current Editor open and use MCP.
- For PIE, viewport, asset, Blueprint, material, Python, log, or automation-test work, discover the relevant VibeUE/Epic toolset before calling it.
- For C++ or plugin changes that require a fresh process, follow the safe rebuild workflow below.
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

After MCP has stopped PIE and saved the intended work, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\operate-webtoue-editor\scripts\Invoke-WebToUEEditorLifecycle.ps1 -Action SafeBuildAndLaunch -AssetsSaved
```

The script performs these guarded steps:

1. Require exactly one running Unreal Editor at most.
2. Require the running PID to have a valid readiness signal belonging to this project.
3. Require `-AssetsSaved` when an Editor is running.
4. Archive `Saved/Logs` under `Saved/VibeUE/LifecycleArchives/<UTC timestamp>/Logs`.
5. Request a normal window close and wait at most 60 seconds.
6. Stop before VibeUE if the process remains alive; never reach its force-kill fallback.
7. Invoke `Plugins/VibeUE/BuildAndLaunchGame.ps1` for the supported build and launch path.
8. Parse the new `Editor-PID`, wait on filesystem events for its readiness file, validate PID/session time, then make one MCP initialization request.

Pass optional build switches only when the request calls for them:

- `-StrictRebuild`: fully recompile VibeUE under warnings-as-errors.
- `-Clean`: remove project/plugin build artifacts before compiling; this is destructive and requires explicit user authorization.
- `-SkipBuild`: relaunch without compiling.

Do not run VibeUE's build script directly while an Editor is open; it force-terminates processes after its own timeout.

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
- Stop when the new Editor exits, readiness exceeds 180 seconds, the signal is stale/invalid, or MCP initialization fails.
- Retry an unchanged failing operation at most twice, then report the evidence and blocker.
- Keep the Editor open at handoff unless the user explicitly asks to close it.
