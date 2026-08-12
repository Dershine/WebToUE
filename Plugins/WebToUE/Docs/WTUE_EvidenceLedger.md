# WebToUE 工程证据账本

> 文档职责：长期保存会改变工程判断的历史验证、性能基线、门禁演进与里程碑证据。
>
> 当前状态与路线入口：[WTUE_TechnicalSummary.md](WTUE_TechnicalSummary.md)
>
> 精确产品边界：[WTUE_SupportMatrix.md](WTUE_SupportMatrix.md)

本账本以时间语境保存事实，不用新口径反向改写历史测试数或性能结果。新增记录必须包含日期、真实 Git 基线、环境/政策（如适用）、可复现证据和工程影响。当前结论以 Technical Summary 为准；本文件用于追溯“结论如何形成”。

## 1. 完成里程碑证据

### M0——技术闭环 8 / 8

- HTML/CSS 可导入。
- 可生成 Unreal Asset。
- Yoga 可计算布局。
- Slate 可原生绘制。
- UMG 可承载 WTUE View。
- UObject 绑定和语义事件闭环。
- 文件变化可重导入。
- Cooked 运行不依赖 UI Source 或浏览器内核。

### M1——UI 基础语义 10 / 10

- 多来源 CSS 级联和精确诊断。
- 约束文本测量和自动换行。
- 稳定 FText/String Table 身份。
- 基础富文本 run。
- 垂直滚动、嵌套边界和裁剪命中。
- 鼠标和基础键盘交互。
- 资产自定义版本和旧资产识别。
- Development/Shipping Cook 历史闭环。
- Core/Runtime/Editor 自动化测试基础。
- 工程技术总览和明确非目标。

## 2. 性能测量政策与固定语料

正式可比较环境指纹为 `79E20297`：UE `5.8.1-56057345`、Windows 11 25H2、i7-12700KF（12 核/20 线程）、32 GB、RTX 5050、Win64 WindowsEditor Development。Sampling policy schema `1` 使用 1 次 warmup、20 个计量样本、median P50 和 nearest-rank P95；当前 budget policy schema `6` 分离 Observe/Enforce，只对稳定达标路径启用硬门。

固定 Compile Corpus 为 100 节点/50 规则、500/200 和 2,000/500。M2 七阶段为 Hydrate、Style、Measure、Layout、Paint Build、Hit Test、Binding；阶段计时为 inclusive，不得相加作为端到端时长。工作量计数覆盖 Hydrate 节点/规则、Style 节点访问、Selector 求值/匹配、Yoga 节点、Text Layout 构建/计算、Brush 和 WebToUE 标记分配。Snapshot Telemetry schema `2` 通过 UE 原生 `SetTelemetryStorage` / `AddTelemetryData` 输出到 `Saved/Automation/Telemetry/WebToUEPerformance.csv`；Core/Runtime 不做文件 I/O，capture 热路径不枚举 Telemetry 也不计算分位数。

`tracked_allocations` 和已知 payload 字节只覆盖 WebToUE 明确标注、可归因的分配点，不等同于进程级 malloc、完整常驻内存或 Slate/Yoga 内部分配。

### 固定工作量基线

| 场景 | Style 节点访问 | Selector 求值 | 匹配 |
| --- | ---: | ---: | ---: |
| 100 节点 / 50 规则 | 100 | 5,000 | 138 |
| 500 节点 / 200 规则 | 500 | 100,000 | 694 |
| 2,000 节点 / 500 规则 | 2,000 | 1,000,000 | 2,794 |

三节点 Runtime 集成语料验证 3 个 Hydrate 节点、1 条规则、3 个 Yoga 节点、1/2 次 Text Layout 构建/计算、4 个 Brush 和 20 个标记分配事件；Runtime NodeState 与 Runtime Render Data 两个连续 payload 事件合计 1,416 B。

专项场景的可比较语义也属于采样政策：Hover 排除 Hit Test Grid/Draw Element List 夹具构造；FieldNotify 排除等长文本重置、稳定 Paint 和同样的夹具构造；暖布局先预热文本缓存；未变化 Paint 是 Slate 实际请求重绘时的 Paint，不是未进入 Paint 的空闲帧或 global invalidation cache 命中。

## 3. 性能证据演进

| 日期/基线 | 证据 | 工程结论 |
| --- | --- | --- |
| 2026-08-11 `342c04a` + working tree | 七阶段插桩前/后 10 样本 median/P95：`0.600095/0.605533 s → 0.611922/0.619202 s`，变化 `+1.97%/+2.26%`。 | 插桩开销低于临时 `+5%/+10%` 守门；数据早于正式采样政策。 |
| 2026-08-11 `fa4929a` + working tree | 正式 Compile Style P50/P95：100/50 `2.7250/2.9380 ms`，500/200 `50.2063/51.0051 ms`，2,000/500 `493.1027/494.4741 ms`；同会话重复运行 P50 漂移 `-0.08%～+0.55%`、P95 `+0.40%～+2.64%`。 | 采样规则和输出可重复；不代表 Runtime 局部更新达标。 |
| 2026-08-11 `5179ec4` + working tree | 500/200 Hover 两次 P50/P95 `55.6425/56.8578`、`55.6616/56.8824 ms`；完整回归 `55.7110/56.1389 ms`。阶段 P95：Style `51.4749`、Measure `3.2947`、Layout `4.0031`、Paint `4.6945`、Hit Test `0.0244 ms`；每样本 500 Style 节点、100,000 Selector 求值、500 Yoga 节点。 | 首个真实 Runtime 局部状态基线；全树 Style 是主要瓶颈，`<0.5 ms` 未达标。 |
| 2026-08-11 `5179ec4` + working tree | 500/200 FieldNotify 两次 P50/P95 `55.1823/56.1490`、`55.6826/57.4543 ms`；完整回归 `55.1790/56.8699 ms`。阶段 P95：Binding `52.3142`、Style `51.4202`、Measure `3.5181`、Layout `4.3019`、Paint `5.3780 ms`；每样本一次 Binding、500 Style 节点、100,000 Selector 求值、500 Yoga 节点。 | 单字段更新仍放大为全树刷新，`<0.5 ms` 未达标。 |
| 2026-08-11 `5179ec4` + working tree | 500 节点暖布局两次 P50/P95 `0.657301/0.716001`、`0.662152/0.692900 ms`；完整回归 `0.668652/0.802100 ms`。每样本 1 Layout、249 Measure、500 Yoga、0/249 Text Build/Compute、749 标记分配。 | 暖布局满足 `<2 ms`，但仍重建完整 Yoga Tree。 |
| 2026-08-11 `5179ec4` + working tree | 未变化 Paint 两次 P50/P95 `0.252949/0.287399`、`0.255549/0.273000 ms`；完整回归 `0.259651/0.335000 ms`。每样本 0 Tick、1 Paint、249 Text Compute、0 Text Build、250 标记分配。 | 时间稳定，但子数组复制未满足零临时分配。 |
| 2026-08-11 `5179ec4` + working tree | Snapshot/budget schema `2/4` 精确覆盖 250/250 个 Paint 子数组 payload，20 样本均为 7,984 B。 | 建立首个可归因字节基线；不代表 allocator 总字节。 |
| 2026-08-11 `72533da` + working tree | Budget schema `5` 将暖布局 `<2 ms` 设为强制门；两次 P95 `0.803798/0.677601 ms`，完整回归 `0.776399 ms`。 | 首个强制时间门禁建立。 |
| 2026-08-12 `0687325` + working tree | Paint Order Cache 前 `0.253649/0.289399 ms`、250 次/7,984 B；后两次 `0.231652/0.248600`、`0.233900/0.289701 ms`，完整回归 `0.238301/0.266302 ms`、0 次/0 B。 | 未变化 Paint 零分配/零已知 payload 成为第二项强制门禁。 |
| 2026-08-12 `3a9b800` + working tree | Runtime Instance 拆分后完整回归 P95：Hover `56.711402`、FieldNotify `56.511901`、暖布局 `0.762999`、未变化 Paint `0.262398 ms`。 | 生命周期隔离成立，硬门继续通过；不宣称性能改善。 |
| 2026-08-12 `18226f9` + working tree | Render Data 拆分后完整回归 P95：Hover `59.009600`、FieldNotify `59.390798`、暖布局 `0.760999`、未变化 Paint `0.277702 ms`。 | Style/Layout 双实例隔离成立；同会话有漂移，不宣称性能改善。 |
| 2026-08-12 `82365d3` + working tree | Presentation 拆分前后 P95：Compile Style small `2.974400→2.903600`、medium `52.610500→53.516100`、stress `512.629500→524.002800`；FieldNotify `56.634001→58.069199`、Hover `56.666899→58.171101`、暖布局 `0.701401→0.735801 ms`；未变化 Paint仍为 0 次/0 B。 | M2.1 只建立职责与所有权边界；硬门继续通过，不宣称性能改善。 |
| 2026-08-12 `b7191ff` + working tree | Ordered Declaration 完整回归 P95：FieldNotify `57.878997`、Hover `62.189799`、暖布局 `0.703800 ms`；未变化 Paint `0.270400 ms`、0 次/0 B。 | 正确性/IR 迁移未破坏两个既有硬门；Hover/FieldNotify 仍未达标，不宣称性能改善。 |
| 2026-08-12 `b7191ff` + working tree | 发布收口后完整回归 P95：FieldNotify `57.034999`、Hover `59.194099`、暖布局 `0.790901 ms`；未变化 Paint `0.277702 ms`、0 次分配。 | 26/26 通过，两个既有硬门继续满足；这是正确性与发布复核，不宣称性能改善。 |

## 4. 发布与工具链证据

- 2026-08-11 的 Development 和 Shipping IoStore 历史验证未发现 WebUI 源文件、Chromium、CEF 或 WebBrowser 运行文件。这是历史阴性证据，发布候选版仍必须重跑 Cook/IoStore/BuildPlugin。
- Editor-only VibeUE 5.0 vendored 提交为 `24ac69d750c1c558a1b78ed5b60644ce000198d3`，版本与归档校验保存于项目根 `Plugins/VibeUE.version.json`。2026-08-11 历史验证覆盖 Win64 BuildPlugin、WebToUEEditor Development 构建、85 个 Agent Skills、83 个 Toolsets、Python API 发现/执行、PerformanceService、13/13 WebToUE 和 17/17 VibeUE 测试；这些数字是当时快照，不代替当前 26/26 WebToUE 回归。
- Editor 生命周期包装器的非沙箱边界、关 Editor 前 Preflight、项目互斥锁、`Saved/VibeUE/Lifecycle/operation.json` 持久状态和 Pester 3/3 是 R-10 的缓解证据；长期决策见 [ADR-0001](ADRs/ADR-0001-Editor-Lifecycle-Execution-Boundary.md)。
- 2026-08-12 `b7191ff` + working tree 的 Win64 Development Cook 保存了 576 个包（569 cooked），但 commandlet 因既有 `GameFeatureData` Asset Manager 配置错误及健康 Editor 占用 MCP `127.0.0.1:8000` 而以 ExitCode 25 失败；Stage/Pak/IoStore 未执行，此记录不得视为发布通过。
- 2026-08-12 同一 `b7191ff` + working tree 后续在 `DefaultGame.ini` 添加 UE 5.8 `GameFeatureData` AlwaysCook 规则，并以 `-AdditionalCookerOptions=-ModelContextProtocolPort=8001` 隔离 Commandlet 端口；Win64 Game + Editor Development 构建、Cook（0 errors）、Stage、Pak、IoStore 全部通过。IoStore 写入 569 packages、2,226 chunks，总容器约 `250.11 MiB`；UAT ExitCode 0。对 2,225 条项目 IoStore 文件清单的路径审计未发现 WebUI、Chromium、CEF、WebBrowser、HTML/CSS/JS；Loose Stage 同样无 WebUI/浏览器运行文件，仅有 UE 自带诊断工具 `Engine/Extras/GPUDumpViewer/GPUDumpViewer.html`，不属于 WTUE UI Source 或 Runtime 依赖。

## 5. 工程变更记录

只记录会改变工程判断、架构、里程碑或支持边界的变化；普通提交不在此重复 Git 历史。

| 日期 | 基线 | 变化 | 路线影响 |
| --- | --- | --- | --- |
| 2026-08-12 | `b7191ff` + working tree | Asset Manager 的 `GameFeatureData` 规则进入正确的 Game 配置层；Cook commandlet 使用独立 MCP 端口；两个 EditorContext Runtime 测试同时受 `WITH_EDITOR` 保护。Win64 Game + Editor build、Cook/Stage/Pak/IoStore 和完整 26/26 通过。 | Ordered Declaration 完成验收，M2.2 进入 🚧 1/5，R-07 降为 Mitigated；宏观 M2 保持 3/7。 |
| 2026-08-12 | `b7191ff` + working tree | Core/Factory/Compiled IR/Hydration 改用有序有效声明；资产版本升至 `OrderedDeclarations`，两个示例资产由源文件重编译；缺源诊断保留 last-good；Editor watcher 排除 transient Automation Document。Win64 Editor build、聚焦 4/4 和 3/3、完整 26/26 通过。 | M2.2 进入 🚧 0/5，R-07 已实现缓解但因当前 Cook/IoStore 门失败仍未验收；宏观 M2 保持 3/7。 |
| 2026-08-12 | `982db64` + working tree | 调整 M2.2 的依赖顺序：Ordered Declaration → Property ID/Typed Value → 属性影响元数据 → Selector Index 工作量门 → 类型化 Cascade；增加资产版本中途门和 Paint-only 资源安全门。 | M2.2 仍为 0/5，宏观路线、预算和 M2.4 Resource Cache 归属不变。 |
| 2026-08-12 | `82365d3` + working tree | 拆分 Core CSS Property、Style Resolver、Yoga Layout Adapter；新增每视图 Presentation Cache 与 `RuntimePresentationIsolation`。Win64 Editor build、聚焦 4/4、性能 5/5、完整 23/23 通过。 | M2.1 6/6，M2 3/7；R-03/R-05 降为 Mitigated；资产 schema 不变。 |
| 2026-08-12 | `18226f9` + working tree | Computed Style/Layout Result 移入每视图连续 Render Data；新增 `RuntimeCacheSeparation`，完整 22/22 通过。 | M2.1 5/6；资产 schema 不变。 |
| 2026-08-12 | `3a9b800` + working tree | 新增每视图 Runtime Instance 和连续 NodeState；新增 `RuntimeInstanceIsolation`，完整 21/21 通过。 | M2.1 4/6；资产 schema 不变。 |
| 2026-08-12 | `d1cdda3` + working tree | Compiled payload 私有化并建立原子提交；失败重导入保留 last-good IR；完整 20/20 通过。 | M2.1 1/6；资产版本和旧资产行为不变。 |
| 2026-08-12 | `c89a728` + working tree | 校正 M2.0 与宏观性能可观测性退出门映射。 | M2 宏观由 1/7 修正为 2/7；不代表全量性能预算达标。 |
| 2026-08-12 | `0687325` + working tree | 校正 M2.0、M2.3、M2.4 与 M2.7 的性能验收归属。 | M2.0 以现有证据达到 6/6，未达预算路径保持未完成。 |
| 2026-08-12 | `0687325` + working tree | View-owned Paint Order Cache 消除未变化 Paint 子数组复制；新增 Paint Order 专项，完整 19/19 通过。 | M2.5 1/5；未变化 Paint 零分配硬门建立。 |
| 2026-08-11 | `72533da` + working tree | Budget schema `5` 明确 Observe/Enforce，暖布局成为首个强制性能门。 | R-06 获得真实阻止回归的时间门禁。 |
| 2026-08-11 | `5179ec4` + working tree | Snapshot/budget schema `2/4` 建立 Paint payload 字节基线。 | R-06 获得首个精确字节覆盖子集。 |
| 2026-08-11 | `5179ec4` + working tree | 建立未变化 Paint、暖布局、FieldNotify、Hover 四个 Runtime 专项。 | 量化 R-01/R-02/R-06，明确全树 Style 与 Yoga 工作量。 |
| 2026-08-11 | `fa4929a` + working tree | 建立正式采样政策与三档 Compile Style 分位数基线。 | M2.0 5/6。 |
| 2026-08-11 | `0d7dd9f` + working tree | 建立稳定性能指标枚举和 UE 原生 Automation Telemetry CSV。 | M2.0 4/6。 |
| 2026-08-11 | `57a334d` + working tree | 增加固定 Corpus 可归因工作量计数和 capture 隔离测试。 | M2.0 3/6。 |
| 2026-08-11 | `342c04a` + working tree | 建立 Editor 生命周期非沙箱边界、Preflight、互斥和持久 Operation State；新增 ADR-0001 与 Pester 3/3。 | R-10 降为 Mitigated。 |
| 2026-08-11 | `342c04a` + working tree | 为七阶段增加 Stats、Trace 与 Automation capture。 | M2.0 2/6。 |
| 2026-08-11 | `2674521` + working tree | 接入固定 VibeUE 5.0，验证 MCP、BuildPlugin、项目构建及 VibeUE/WebToUE 测试。 | 获得 Editor 调试能力，不改变产品协议边界。 |
| 2026-08-11 | `2674521` + working tree | 建立确定性 Benchmark Corpus；将技术总结重构为长期工程事实与路线入口。 | M2.0 1/6，M2 成为唯一活跃里程碑。 |
| 2026-08-11 | `2674521` | 稳定 FText/String Table 身份与基础富文本。 | 完成 M1 本地化与文本语义。 |
| 2026-08-11 | `325d17b` | 垂直滚动、嵌套边界和裁剪命中。 | 完成 M1 基础滚动语义。 |
| 2026-08-10 | `e23638a` | 约束文本测量与自动换行。 | 完成 M1 排版基础。 |
| 2026-08-10 | `d81ec1c` | 多来源 CSS 诊断。 | 完成 M1 样式诊断基础。 |
| 2026-08-10 | `f5be9a5` | Compiled Document 自定义版本。 | 建立 M6 资产兼容前置能力。 |
| 2026-08-10 | `84f2eee` | 原生 HTML/CSS UI Preview 首版。 | 完成 M0 技术闭环主体。 |

## 6. 历史固定成本

2026-08-11 的 Win64 Editor DLL 文件体积：

| 模块 | 大小 |
| --- | ---: |
| `UnrealEditor-WebToUECore.dll` | 240,640 B |
| `UnrealEditor-WebToUEEditor.dll` | 211,968 B |
| `UnrealEditor-WebToUERuntime.dll` | 307,200 B |
| `UnrealEditor-WebToUEYoga.dll` | 340,480 B |
| 合计 | 1,100,288 B（约 1.05 MiB） |

这只能证明当时插件代码的固定文件成本很小，不代表 Shipping 包体、运行时内存、GPU 成本或 Gameface 性能对等。
