# ADR-0001：Editor 生命周期执行边界

- 状态：Accepted
- 日期：2026-08-11
- 范围：WebToUE 开发、构建与 Editor Automation；不进入 Core、Runtime、Cooked 游戏或产品协议

## 背景

WebToUE 的 C++ 重编译需要关闭当前 Editor，再通过 VibeUE 的受支持脚本构建并启动新进程。UE 5.8 的 UBT、Turnkey、UBA、Zen 和 Editor 启动期子进程会写入项目目录之外的位置，包括 Engine `Intermediate`、`%LOCALAPPDATA%\UnrealBuildTool` 和 `%LOCALAPPDATA%\UnrealEngine`。

当生命周期命令从 workspace-only sandbox 启动时，项目目录内写入可以成功，但 UBT 可能在轮换 `Trace*.uba` 时抛出 `UnauthorizedAccessException`。Editor 继承相同受限边界后，其 Turnkey SDK 探测也会失败，导致进程没有主窗口、VibeUE readiness 或 MCP。若调用方把短观察超时误判为命令完成并重新运行，还会产生并发 UBT、重复 CLR 弹窗和孤儿进程。

## 决策

1. 继续使用唯一的 `$operate-webtoue-editor` Skill；不创建第二个 Editor 生命周期 Skill。
2. UBT、VibeUE build/launch 和 UnrealEditor 必须从同一个、仅授权项目生命周期脚本的非沙箱边界启动。不得为此关闭全局沙箱或授权任意 PowerShell。
3. 生命周期包装器在归档日志或关闭健康 Editor 之前，必须真实创建并删除写入探针，验证项目 `Saved/Intermediate`、Engine `Intermediate`、UnrealBuildTool LocalAppData 和 UnrealEngine LocalAppData。
4. 每个项目同时只允许一个生命周期操作。包装器使用项目级命名互斥锁，并将 OperationId、阶段、所有者 PID、VibeUE PID、Editor PID、输出路径和终态原子写入 `Saved/VibeUE/Lifecycle/operation.json`。
5. 调用方遇到长时间无输出或执行单元 yield 时，只能等待同一个执行单元或读取操作状态；不得重新发起 build/launch。包装器还会拒绝仍有存活 owner、VibeUE 或 Editor PID 的旧操作。
6. Editor 关闭仍只允许 MCP 保存门和正常窗口关闭；不因构建、readiness 或 MCP 失败而自动强杀 Editor。
7. 非标准 Engine Root 由项目包装器显式验证。当前兼容层通过可清理的临时同目录副本向 vendored VibeUE 脚本注入路径；正常路径使用 `finally` 清理，后续操作还会在确认没有存活生命周期 PID 后清理精确前缀的中断残留。长期应由 VibeUE 上游提供正式 `-EngineRoot` 参数，随后更新项目锁定版本并移除兼容层。

## 被否决的替代方案

- **全局关闭沙箱**：权限范围远大于 UE 生命周期所需，无法形成最小授权边界。
- **只让 UBT 在非沙箱运行**：Editor 与 Turnkey 仍会继承受限父进程，不能解决启动期失败。
- **重定向整个 `LOCALAPPDATA` 到项目目录**：会改变 UE、Zen、SDK 和用户级缓存语义，验证结果不再代表正常开发环境。
- **新增第二个 Skill**：会形成两个相互竞争的路由和安全规则来源。
- **无状态地重试脚本**：无法区分慢构建、孤儿子进程与真实失败，会重复启动 UBT。
- **直接修改 vendored VibeUE**：破坏第三方版本和哈希边界；仅接受经过单独验证的上游升级。

## 结果与约束

- 受限环境中的 Preflight 会在 Editor 仍开启时失败，并给出具体不可写目录。
- 生命周期命令需要一次精确的非沙箱批准，但其他源码、Git 和文档工作继续留在 workspace sandbox。
- 操作状态属于 `Saved` 下的瞬态证据，不进入产品协议或 Runtime。
- build、readiness、MCP、Python、World 和 Automation Test 仍是独立验收门。
- 外部调用方若主动终止生命周期所有者进程，包装器只能阻止仍存活子进程期间的重复启动；调用方仍应优先续接原执行单元。

## 验收

- Pester：可写隔离环境 Preflight 通过；缺失探针目录时在创建 operation state 前失败；持久状态可识别存活所有者且中断残留只按精确临时前缀清理。
- 受限 workspace 实测：Engine `Intermediate` 和两个 LocalAppData 探针失败，Editor 未启动或关闭，且无探针残留。
- 真实集成门：在精确批准的非沙箱边界完成 UE 5.8 Win64 Development build/launch，并分别验证 readiness、MCP、Python、World 和相关 Automation Test。
