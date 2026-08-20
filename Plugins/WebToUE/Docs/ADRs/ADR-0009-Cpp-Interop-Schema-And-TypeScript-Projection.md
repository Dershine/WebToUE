# ADR-0009：C++ Data/Command Schema 与 TypeScript 投影边界

- 状态：Accepted
- 日期：2026-08-20
- 范围：项目 C++ Data/Command Schema 的单一事实源、版本演进、规范快照和 `.d.ts` 投影方向；不接入现有 FieldNotify/View、Command dispatch、Behavior Compiler 或资源 freshness

## 背景

ADR-0004 已决定 C++ 协作使用显式版本化 Data/Command Schema，直接 C++/FieldNotify 是基础适配，MVVM 只作为可选 Adapter。当前实现却只有 Session 持有的弱 `UObject` Data/Command Context、Compiled Binding Op 中无类型的根 `FName`，以及只有生命周期 token、没有 Command ID/payload 合同的异步协调器。若以后分别从 UHT reflection、MVVM ViewModel、手写 TypeScript 和 UI Compiler 配置生成协议，会出现多个事实源、大小写/类型漂移和 UHT→UBT→UI Compiler→生成 C++ 的循环依赖。

M3.8 先建立不依赖现有 View 产品接入的纯 C++ Policy：项目代码声明一次 Schema，Core 生成规范快照，Runtime/Compiler 以后消费同一快照，Editor 工具只从快照派生 `.d.ts`。资源依赖 Hash、Cook freshness、多层 IR 版本和实际 Behavior/Binding/Command 接入仍由后续路线验收。

## 决策

1. 项目 C++ 构造的 `FWebToUEInteropSchemaDescriptor` 是 Data/Command Schema 的唯一事实源。UI Source、Behavior Source、`.d.ts`、MVVM 元数据、UHT reflection 和 Compiled UI/Behavior IR 都是消费者或 Adapter，不能反向生成或静默修改该 descriptor。
2. Descriptor、规范快照、验证和版本演进 Policy 位于最低共同依赖 `WebToUECore`，只使用纯值类型；`WebToUERuntime` 与 `WebToUEEditor` 继续单向依赖 Core。项目模块可以构造 descriptor 并把快照显式交给 Compiler/Session Adapter，不需要 Core 反向依赖项目、Editor、MVVM 或生成文件。
3. P0.5 值代数关闭为 `void`（仅 Command request/result）、`bool`、`int32`、`float32`、`float64`、`string`、`FName`、`FText`、具名 Enum/Record、只读 Array 和 Optional。任意 UObject/UFunction、raw pointer、动态 Variant、Map、递归 Record 和未知 Named Type 均不进入此合同；以后扩展必须带版本、诊断和消费证据。
4. 根 Data Field 声明稳定名称、类型和 FieldNotify observability；具名 Record Field 不拥有独立观察订阅。Command 声明稳定名称、request type、`none/immediate/async` response mode、result type 与 cancellable 位。它是静态协议元数据，不授权按字符串反射调用 UObject/UFUNCTION，也不证明 Gameplay 或网络权限。
5. `BuildSnapshot` 拒绝无效 Schema ID/版本/标识符（`WTUE-SCHEMA-001`）、UE 语义下仅大小写不同的重复名称/重复 enum wire value（`WTUE-SCHEMA-002`），以及未知/递归类型、非法 void、错误 Command response/result/cancel 组合（`WTUE-SCHEMA-003`）。成功后 Enum、Record、Field、Data、Command 和 Enum Member 全部按大小写敏感稳定名称排序；失败不暴露部分快照。
6. Schema 版本为显式 `Major.Minor` 且 Major 非零。完全不变的快照可以保持版本；同 Major 的 Minor 递增只允许新增 Enum Member、Record Field、Data Field 或 Command，已有定义必须逐项相同；删除、改名、改类型、改 wire value、改 response/cancellation 必须提升 Major。版本倒退、同版本漂移或 Minor breaking change 以 `WTUE-SCHEMA-004` 失败关闭。
7. `WebToUEEditor` 的 `FWebToUESchemaTypeScriptEmitter` 只接受已验证、规范排序的 snapshot，并执行无文件 I/O 的确定性内存投影。输出声明 Schema ID/version、只读 Data、Enum/Record 和 Command metadata map；它不生成函数调用面、不执行 TypeScript，也不写 C++、`.generated.h`、Build.cs 或 UHT/UBT 输入。
8. UI/Behavior Compiler 以后直接消费 C++ snapshot 进行名称/类型解析和 IR lowering；`.d.ts` 只服务编辑器、IDE 和作者静态检查，不能再被读回作为 Compiler 事实源。输出文件路径、原子写入、dependency hash、DDC/lockfile 和 stale-source Cook 门仍归 M3.9/M6，不由本路线暗中定义。
9. 直接 C++ Adapter 是 0.5 基础路径：它必须把具体 Data/Command Context 显式验证并绑定到 snapshot。UHT/FieldNotify 可以帮助 Adapter 定位已声明字段，但 reflection 不能自动扩大 Schema；MVVM 可以订阅/转发同一 snapshot 中的字段和命令，但不是 Schema owner、Runtime 依赖或编辑器配置前置。
10. 本路线不修改现有 `FWebToUESession`、`FWebToUECompiledBindingOp`、`FWebToUEAsyncCoordinator`、资产版本、Cook 数据或模块依赖。当前 text/visible/enabled FieldNotify、字符串 Click Event 和无 payload 的异步 Command token 仍保持原能力边界；M5 必须另行完成真实类型化 Adapter、事务/异步接入和敌意 payload 测试。

## 被否决的替代方案

- **从 `.d.ts` 或 JSON Schema 生成 C++/UHT 类型**：会让前端产物成为 UBT 输入，形成需要先编译工具才能编译工具本身的循环，并把 Gameplay C++ 权威交给生成文件。
- **把 UHT reflection 自动扫描结果当作 Schema**：接入快，但会把未显式授权的 UObject 属性/UFUNCTION 暴露给 UI，Schema 还会随 Blueprint/reflection 细节漂移。
- **让 MVVM ViewModel 成为唯一 Schema**：把可选 Adapter 变成 Runtime/Editor 配置前置，并让直接 C++ 项目无法共享同一确定性协议。
- **分别手写 C++ 与 `.d.ts`**：简单但天然存在双事实源，无法证明版本和 Command payload 一致。
- **把 emitter 放进 Runtime 热路径或在启动时写文件**：给 Cooked 游戏带入作者工具职责和文件 I/O，破坏模块与生命周期分层。
- **当前路线直接改造 FieldNotify/View/Behavior/Command dispatch**：会同时扩大事务、异步 payload、Compiler lowering、资产和敌意 Runtime 验收，超过“先冻结单一事实源与生成方向”的微观路线。

## 结果与后续约束

- `InteropSchemaCanonicalization` 覆盖规范排序、非零版本、大小写冲突、未知类型和 Command shape 失败关闭；`InteropSchemaEvolution` 覆盖同版本不变、Minor additive、Minor breaking 拒绝和 Major breaking 接受。
- `Editor.InteropSchemaTypeScript` 证明 descriptor 声明顺序变化仍生成 byte-identical `.d.ts`，并守住 C++ source/version、只读 Data/Record、Command metadata 和不含 UHT/UBT 输入标记的边界。
- M5 接入真实 Data/Command Adapter 时必须让 Compiled Binding/Behavior/Command payload 引用 snapshot 中的稳定类型和名字，并继续服从 Session Generation、更新事务、异步 exact-once/cancel 和 Gameplay 权威边界。
- M6 生成/缓存 `.d.ts`、Source Map 或 IDE 服务时必须从 snapshot 单向派生，并把 Schema version/fingerprint 纳入确定性 key；不得读取声明文本反推协议。
- M3.9 的 Resource/IR freshness 合同必须明确 Schema version 与依赖 hash 如何进入 Document/Cook freshness，但不能把资源合同并入本 ADR 的纯 Data/Command Policy。
