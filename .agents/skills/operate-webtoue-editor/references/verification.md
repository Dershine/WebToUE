# Verification and diagnosis

Load this reference for a rebuild/relaunch acceptance pass or when a lifecycle gate fails.

## Acceptance sequence

1. Run the lifecycle script with `-Action Status`.
2. Require one `UnrealEditor` PID, `ReadySignal.Valid = true`, `Mcp.Ready = true`, and top-level `Healthy = true`.
3. Through MCP, discover the current schema before calling a toolset.
4. Run an `execute_python_code` probe beginning with `import unreal`; print engine version, project name, and whether an editor world is available.
5. If the task needs PIE, query `EditorToolset.EditorAppToolset.IsPIERunning`. Start PIE only when required and always stop it before a build.
6. Re-read modified assets, compile Blueprints after structural edits, and print compact evidence for every changed object.
7. Use `AutomationTestToolset.DiscoverTests`, followed by `RunTests` or `RunTestsByFilter`. Record discovered, executed, passed, failed, skipped, and duration values. Any failed test fails acceptance.
8. Use `StartsWith:WebToUE` for the project suite and `StartsWith:VibeUE` for the integration suite when both are relevant. Counts may grow over time; require zero failures instead of hard-coding a count.
9. Capture and inspect a viewport image for visible changes.

## Gate diagnosis

| Gate | Evidence | Action |
| --- | --- | --- |
| Editor absent | Empty `Editors` array | Use `SafeBuildAndLaunch` without `-AssetsSaved`. |
| Ambiguous Editor | More than one PID | Do not close anything; ask the user to identify/close the unrelated session. |
| Invalid signal | Missing, stale, PID-mismatched, or malformed JSON | Treat the Editor as not VibeUE-ready; inspect startup logs without forcing a restart. |
| Graceful close timeout | Old PID remains alive after 60 seconds | Bring the Editor forward and resolve save/modal UI; never force-kill it. |
| Build failure | VibeUE script returns nonzero | Inspect build output and the archived previous logs; fix source before retrying. |
| Readiness timeout | New PID alive but no valid signal within 180 seconds | Inspect the new `Saved/Logs` startup log and plugin load errors. |
| MCP failure | Readiness valid but initialize request fails | Confirm `.codex/config.toml`, MCP Auto Start, port `8000`, and firewall/port ownership. Do not repeatedly poll. |
| Python/world failure | MCP works but probe/world is unavailable | Wait for the specific subsystem only when there is a bounded event/readiness signal; otherwise report it separately. |
| Tests stay queued | Interactive automation command does not start | Use `AutomationTestToolset.DiscoverTests` and `RunTests`/`RunTestsByFilter`. |

## Evidence to report

- Previous and new Editor PID.
- Exact build mode and optional switches.
- Readiness file path, `pluginVersion`, and session start time.
- MCP HTTP status and server identity when returned.
- Python/world probe output.
- Test filter and pass/fail counts.
- Viewport artifact path when applicable.
- Archived pre-relaunch log directory.
