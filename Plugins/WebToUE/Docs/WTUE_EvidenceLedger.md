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

固定 Compile Corpus 为 100 节点/50 规则、500/200 和 2,000/500；Hydration Corpus 为 500/200、2,000/500 和 10,000/500。M2 七阶段为 Hydrate、Style、Measure、Layout、Paint Build、Hit Test、Binding；阶段计时为 inclusive，不得相加作为端到端时长。工作量计数覆盖 Hydrate 节点/规则、Style 节点访问、Selector 候选/求值/匹配、Yoga 节点、Text Layout 构建/计算、Brush 和 WebToUE 标记分配。Snapshot Telemetry schema `3` 通过 UE 原生 `SetTelemetryStorage` / `AddTelemetryData` 输出到 `Saved/Automation/Telemetry/WebToUEPerformance.csv`；Core/Runtime 不做文件 I/O，capture 热路径不枚举 Telemetry 也不计算分位数。

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
| 2026-08-12 `a4e4e98` + working tree | Property ID/Typed Value 实施前→后同环境/政策 P95：Compile Style small `2.956700→2.647200`、medium `51.433500→50.948400`、stress `552.030900→517.156700`；FieldNotify `58.123998→55.596102`、Hover `58.854800→55.614501`、暖布局 `0.717800→0.731699 ms`；未变化 Paint `0.247799→0.266701 ms`，仍为 0 次/0 B。 | 编译期类型化未破坏两个既有硬门；单次同会话样本不作为性能改善声明，Hover/FieldNotify 仍远未达 `<0.5 ms`。 |
| 2026-08-12 当前 HEAD + working tree | Property Metadata 完整回归 P95：Compile Style small `2.624300`、medium `51.806100`、stress `518.105700 ms`；FieldNotify `57.067800`、Hover `56.000602`、暖布局 `0.701901 ms`；未变化 Paint `0.291500 ms`、0 次/0 B。环境 `79E20297`，sampling/budget/snapshot schema `1/6/2`。 | 资源影响分类未破坏两个既有硬门；Hover/FieldNotify 仍访问 500 Style 节点、求值 100,000 次 Selector、重建 500 Yoga，远未达 `<0.5 ms`。样本波动不作为性能改善或回归声明。 |
| 2026-08-13 当前 HEAD + working tree | Selector Index 实施前→后同环境/政策 P95：Compile Style small `2.624300→0.117600`、medium `51.806100→0.959600`、stress `518.105700→7.316700 ms`；FieldNotify `57.067800→5.429000`、Hover `56.000602→6.182000 ms`。候选/求值从全扫描 small `5,000`、medium `100,000`、stress `1,000,000` 降为 `638`、`10,694`、`102,794`，且每档候选数等于实际求值数。暖布局 `0.747699 ms`；未变化 Paint `0.283401 ms`、0 次/0 B。环境 `79E20297`，sampling/budget/snapshot schema `1/6/3`；schema 3 仅新增候选计数，既有时间和工作量语义不变。 | Selector 主桶显著减少全规则扫描，并满足 500/200 `<100,000` 工作量门；两个既有硬门继续通过。Hover/FieldNotify 仍访问 500 Style 节点并重建 500 Yoga，分别高于 `<0.5 ms`，故不把本结果外推为局部刷新预算达标。 |
| 2026-08-13 `db98aabaa263d002da9bff29a452169e4209a23f` + working tree | Typed Cascade 实施前→后同环境/政策 P95：Compile Style small `0.134400→0.097000`、medium `1.008100→0.901200`、stress `7.054000→6.689500 ms`；Hover `5.816400→5.415700`、FieldNotify `5.780801→6.068401`、暖布局 `0.718202→0.765499`、未变化 Paint `0.246599→0.275400 ms`，后两项继续满足硬门且 Paint 为 0 次/0 B。三节点集成语料三轮 Style Resolve 的标记分配 `20→17`。环境 `79E20297`，sampling/budget/snapshot schema `1/6/3`。 | 固定 winner slots 移除每节点 matched-rule 数组增长、排序和临时 Property TMap；精确标记分配减少 3 次。时间样本存在双向漂移，不宣称耗时改善；Hover/FieldNotify 仍访问全树且未达 `<0.5 ms`。 |
| 2026-08-13 `124d315c3ac400967492f9a530f0e992b48a3da0` + working tree | 单次显式 `SetDocument` 的 Hydrate P50/P95：500/200 `2.258901/2.423998 ms`、2,000/500 `14.250049/14.574900 ms`、10,000/500 `73.993750/75.649202 ms`。首 View 与第二 View known-owned 容量均分别为 `895,678/3,227,058/13,966,944 B`；4-View Editor RSS 放大斜率为 `3,072/1,110,016/7,009,280 B/View`。环境 `79E20297`，sampling/snapshot schema `1/3`，聚焦 1/1 与完整 33/33 通过。 | 当前深拷贝 Hydration 的时间与可归因容量随规模增长，第二 View 不共享这部分容量；RSS 小档受 allocator/页粒度噪声影响。该数据只完成 M2.3 测量前置，不裁决共享模板，也不代表 Packaged RSS/LLM。 |
| 2026-08-13 `679285a` + `ff69bd1` + M2.3 documentation closure | 文档修订级 Runtime Style Template 共享已水合 Rules 与 Selector Index。500/2,000/10,000 Hydrate P50/P95 为 `2.157299/2.266999`、`14.695600/15.088700`、`75.147599/76.560199 ms`；共享模板容量 `84,496/205,456/205,456 B`，每 View known-owned `691,958/2,761,322/13,826,312 B`，第二 View 不再重复共享容量。完整回归同时记录 Hover `5.536500`、FieldNotify `5.673200`、暖布局 `0.764202`、未变化 Paint `0.249200 ms` 且 0 次/0 B。环境 `79E20297`，sampling/budget/snapshot schema `1/6/3`。 | M2.3 的共享边界有定量依据；相比稳定身份基线，每 View 省去规则/索引复制，两个既有硬门保持通过。节点包装仍按 View 与 N 线性增长，Editor RSS 仍不替代 Packaged RSS/LLM；不把时间波动宣称为产品级性能改善。 |
| 2026-08-13 `38b880b`～`492019b` + M2.4 documentation closure | 500/200 单节点 Hover P50/P95 为 `0.271350/0.294100 ms`，目标 `<0.5 ms` 已 Enforce。K=1 每样本改变 2 个旧/新状态路径节点，检查 160 个失效目标候选，访问 1 个 Style 节点、候选/求值 43/43、匹配 4、Dirty Target 1、Property change 1、Yoga 0、Text Cache invalidation 0、Paint Order rebuild 0；目标背景 Brush build 1、标记分配 1。完整 Paint 仍有 249 次 Text Desired Size compute。环境 `79E20297`，sampling/budget/snapshot schema `1/7/4`。 | Hover 的 Cascade、Measure/Layout 和 Presentation Cache 失效已与实际影响目标成正比，时间预算成为硬门；Paint/Hit Test 仍全树递归，Editor Development 微基准不代表 Packaged RT/GPU、输入到像素或产品对等。FieldNotify P95 `5.039498 ms` 继续 Observe 并留给 M2.5。 |

## 4. 发布与工具链证据

- 2026-08-11 的 Development 和 Shipping IoStore 历史验证未发现 WebUI 源文件、Chromium、CEF 或 WebBrowser 运行文件。这是历史阴性证据，发布候选版仍必须重跑 Cook/IoStore/BuildPlugin。
- Editor-only VibeUE 5.0 vendored 提交为 `24ac69d750c1c558a1b78ed5b60644ce000198d3`，版本与归档校验保存于项目根 `Plugins/VibeUE.version.json`。2026-08-11 历史验证覆盖 Win64 BuildPlugin、WebToUEEditor Development 构建、85 个 Agent Skills、83 个 Toolsets、Python API 发现/执行、PerformanceService、13/13 WebToUE 和 17/17 VibeUE 测试；这些数字是当时快照，不代替当前 30/30 WebToUE 回归。
- Editor 生命周期包装器的非沙箱边界、关 Editor 前 Preflight、项目互斥锁、发布宿主/已观察 RunUAT-UBT-Commandlet 进程树 PID 与日志、`Saved/VibeUE/Lifecycle/operation.json` 持久状态和 Pester 5/5 是 R-10 的缓解证据；长期决策见 [ADR-0001](ADRs/ADR-0001-Editor-Lifecycle-Execution-Boundary.md)。`SafeBuildCookAndLaunch` 的真实 UAT→Editor 重启集成本轮未执行，不能仅凭脚本测试记为发布通过。
- 2026-08-12 `b7191ff` + working tree 的 Win64 Development Cook 保存了 576 个包（569 cooked），但 commandlet 因既有 `GameFeatureData` Asset Manager 配置错误及健康 Editor 占用 MCP `127.0.0.1:8000` 而以 ExitCode 25 失败；Stage/Pak/IoStore 未执行，此记录不得视为发布通过。
- 2026-08-12 同一 `b7191ff` + working tree 后续在 `DefaultGame.ini` 添加 UE 5.8 `GameFeatureData` AlwaysCook 规则，并以 `-AdditionalCookerOptions=-ModelContextProtocolPort=8001` 隔离 Commandlet 端口；Win64 Game + Editor Development 构建、Cook（0 errors）、Stage、Pak、IoStore 全部通过。IoStore 写入 569 packages、2,226 chunks，总容器约 `250.11 MiB`；UAT ExitCode 0。对 2,225 条项目 IoStore 文件清单的路径审计未发现 WebUI、Chromium、CEF、WebBrowser、HTML/CSS/JS；Loose Stage 同样无 WebUI/浏览器运行文件，仅有 UE 自带诊断工具 `Engine/Extras/GPUDumpViewer/GPUDumpViewer.html`，不属于 WTUE UI Source 或 Runtime 依赖。
- 2026-08-12 `a4e4e98` + working tree 在安全关闭 Editor 后完成 Win64 Game + Editor Development build、Cook、Stage、Pak、IoStore；569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0。IoStore/UFS/NonUFS 清单的精确路径审计未发现 WebUI、Chromium、CEF、WebBrowser 或 `.html/.css/.js`；发布后 Editor PID 10604 通过 readiness、MCP、Python/World 和聚焦 7/7。
- 2026-08-12 当前 HEAD + working tree 在安全关闭 Editor 后完成 Win64 Game + Editor Development build、Cook（0 errors）、Stage、Pak、IoStore；569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0。首次尝试因健康 Editor 的 Live Coding 门禁在 build 阶段退出，正常关闭后同命令通过；发布后 Editor PID 35244 通过 readiness、MCP 200、Python 和 `Lvl_TopDown` world readiness。
- 2026-08-13 当前 HEAD + working tree 在安全关闭 Editor 后完成 Win64 Game + Editor Development build、Cook（0 errors）、Stage、Pak、IoStore；569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0。发布后 Editor PID 25000 通过 VibeUE 5.0 readiness、MCP 200、UE 5.8.1 Python/`Lvl_TopDown` world readiness 和 `SelectorIndex` 1/1。

## 5. 工程变更记录

只记录会改变工程判断、架构、里程碑或支持边界的变化；普通提交不在此重复 Git 历史。

2026-08-13 的路线复核将后续 M2 重排为 M2.3～M2.9；此前记录中的 M2.3/M2.4/M2.5 编号保留当时语境，不反向改写历史。

| 日期 | 基线 | 变化 | 路线影响 |
| --- | --- | --- | --- |
| 2026-08-13 | `38b880b`、`741515a`、`c4716d9`、`492019b` + M2.4 documentation closure | 共享 Style Template 编译 Pseudo Reason→Target 依赖，实例 Target Index 以 Handle 定位目标；Typed Cascade 生成 old/new change set，Hover/Active/Focus 只更新旧/新状态路径与实际目标。Presentation 按 Impact 定向保留 Text/Brush/Paint Order/Layout Cache；因果报告提供最小只读文本视图，Compiler 诊断宽泛祖先 Pseudo。新增/扩展依赖、change set、深层祖先/组合/继承、资源与缓存安全、工作量和预算专项。聚焦 2/2、完整 37/37、UE 5.8 Win64 Editor Development 通过；最终 Editor PID `10772` 的 VibeUE 5.0 readiness、MCP 200、Python 5.8.1/`Lvl_TopDown` World 通过。资产 payload/schema、模块依赖和发布边界不变，未运行 BuildCookRun、Packaged Runtime 或 viewport capture。M2.4 的验收集合没有 `P0.5-if-used` 产品能力，故路线内裁决为 `N/A`；冻结 Corpus 的能力级裁决仍留在 M2.8。 | M2.4 达到 ✅ 7/7，宏观 M2 进入 6/9；R-01 的 Hover 分支和 R-04 的 Paint-only 分支获得局部失效证据，FieldNotify、Resource、Display List 与真实渲染风险仍开放。M2.5 是下一依赖，但本次不进入。 |
| 2026-08-13 | `679285a` + `ff69bd1` + M2.3 documentation closure | `UWebToUEDocument` 按修订惰性缓存不可变 Runtime Style Template；多个 View 共享已水合 Rules 与 Selector Index，Commit 使缓存失效。Runtime State/Render Data、Yoga Measure、Text、Brush 与 Paint Order 统一使用 Instance Handle；`RuntimeIdentity` 新增结构修订、`NotifyDocumentChanged` 重导入、View 销毁和共享模板专项。聚焦 4/4、6/6，完整 34/34，UE 5.8 Win64 Editor Development 两次受保护增量编译通过；最终 Editor PID `12360` 的 VibeUE 5.0 readiness、MCP 200、Python/World 均通过。资产 payload/schema、模块依赖和发布边界不变，未运行 BuildCookRun 或 Packaged Runtime。M2.3 不包含由目标游戏 Corpus 触发的可选产品能力，因此本路线 `P0.5-if-used` 为 `N/A`；能力级 Corpus 裁决仍留在 M2.8。 | M2.3 达到 ✅ 6/6，宏观 M2 进入 5/9；R-11 从 High 降为 Medium/Observe。M2.4 是下一依赖，但本次不进入；Packaged 每 View/第二 View RSS/LLM 仍由 M2.7/M2.9 验证。 |
| 2026-08-13 | `1dc535f277fdab740d3999bbe1e38e95cdd939e8` + roadmap working tree | 将近期交付目标收窄为 PersonalGame-ready 0.5：保留 M2 稳定身份、局部失效、持久 Layout/Text/Resource、Display List 和真实 Win64 Packaged 性能证据；0.5 只吸收真实菜单/HUD 所需的 Command、Component、普通 Keyed List、基础 Transition、手柄/CommonUI、DPI/Safe Zone、Golden、错误恢复与资源上限。BuildPlugin、第二平台、完整触摸/IME/无障碍、虚拟化、通用工具链、稳定外部 API 和分发收口延后到 1.0；Gameface 环境不可获得时记录 `Unknown`。本次仅改变路线与门禁分类，未修改 Runtime/资产，也未运行新的 Automation、Build 或发布门。 | M2 技术地基与预算不降级；M2.9 从外部通用发布门改为 Win64 项目内 0.5 Go/No-Go，减少不阻塞目标游戏的产品化工作。Deferred 不计完成，真实 Corpus 一旦使用按需项即自动提升为阻断门。 |
| 2026-08-13 | `dda125b67c55ffd6c1d04725be115cacfde1cad3` + working tree | Core 新增修订内稳定 Template Node ID 与 Owner + Generation + Slot Instance Handle；每 View Slot Table 统一解析静态/动态节点，交互状态及 Text/Brush/Paint Order Cache 改用 Handle，重水合使旧代次失效。新增 `RuntimeIdentity` 专项，聚焦 10/10、完整 34/34 与 UE 5.8 Win64 Editor Development 已通过；资产 payload/schema、旧资产、模块和发布边界不变。正式 Hydration Corpus 实施前→候选完整回归 P50/P95 为 500/200 `2.258901/2.423998→2.187399/2.256501 ms`、2,000/500 `14.250049/14.574900→15.556302/16.173001 ms`、10,000/500 `73.993750/75.649202→76.497801/78.366399 ms`；每 View known-owned 为 `895,678→916,030`、`3,227,058→3,308,234`、`13,966,944→14,373,224 B`，第二 View 仍等量复制。环境 `79E20297`，sampling/snapshot schema `1/3`。 | M2.3 进入 🚧 2/6；身份与 Cache 生命周期合同完成，但新增身份字段/Slot Table 带来可归因容量增长，且尚未共享静态模板，故不宣称性能改善，R-11 保持 High。共享节点/规则/Selector Metadata 裁决仍是下一依赖。 |
| 2026-08-13 | `124d315c3ac400967492f9a530f0e992b48a3da0` + working tree | 新增固定 500/2,000/10,000 节点 Hydration Corpus、单次显式 Hydrate 分位数、View-owned runtime/presentation 容量 census、首/第二 View 及 4-View Editor RSS Telemetry；观测接口仅在 `WITH_DEV_AUTOMATION_TESTS` 存在。聚焦 1/1、完整 33/33 和 Win64 Editor Development 通过；资产 payload/schema、Shipping API 和发布边界不变。 | M2.3 进入 🚧 1/6；R-11 获得定量证据但保持 High，下一依赖是稳定 TemplateNodeId/Instance Handle 与共享边界裁决。 |
| 2026-08-13 | `db98aabaa263d002da9bff29a452169e4209a23f` + working tree | Style Resolver 改为可复用固定 Property winner slots，按 inline origin、specificity、source order、declaration order 直接竞争；受支持 shorthand 展开到规范 longhand slots，不再创建/排序每节点 matched-rule 数组或构造临时 Property TMap。新增 `TypedCascade` Core 专项与最终 Yoga/Slate 输出专项；聚焦 8/8、固定性能 Corpus 5/5、完整 32/32 和 Win64 Editor Development 通过。资产 payload/schema、模块和发布边界不变，未运行 BuildCookRun 或 Packaged Runtime。 | M2.2 达到 ✅ 5/5，宏观 M2 进入 4/9；R-01/R-06 仍开放，下一依赖为 M2.3 共享模板与稳定身份裁决。 |
| 2026-08-13 | `61dcb0a` + roadmap working tree | 基于 Runtime 热路径、现有 Telemetry 与 Gameface/Slate 官方架构资料复核 M2：新增三类产品性能合同；将共享静态模板/稳定身份、真实 Packaged GT/RT/GPU/Batch/首帧/内存证据提升为宏观退出门；后续改为 M2.3 身份裁决、M2.4 Paint-only、M2.5 FieldNotify/Text、M2.6 Layout/Resource、M2.7 Display/真实渲染、M2.8 产品 Profile、M2.9 Go/No-Go。未修改 Runtime/资产，未运行新的 Automation、Build 或发布门。 | M2 宏观由 `3/7` 重述为 `3/9`，不是进度倒退或新性能结论；冻结大型 CSS/组件/动画扩张直到局部成本和真实渲染上限得到证明。 |
| 2026-08-13 | 当前 HEAD + working tree | Editor 生命周期增加受跟踪 `SafeBuildCookAndLaunch`：固定 Win64 Build/Cook/Stage/Pak/IoStore、持久发布宿主/已观察进程树 PID、AutomationTool 退出码和完整日志，成功后 `-SkipBuild` 恢复 Editor；MCP 改为一次有界延迟复查并区分 `EditorReadyMcpPending`。Pester 5/5；真实 UAT→重启集成未执行。 | 加固 R-10 与发布流程，不改变 Runtime、产品协议或 M2 路线进度；不得把脚本层通过写成发布门通过。 |
| 2026-08-13 | 当前 HEAD + working tree | Core 为每视图 Runtime Document 建立非序列化 Selector Index；每条规则按右端 ID/Class/Tag/Pseudo/Universal 归入单一主桶，节点 ID/Class identity 在初始化时规范化且重复 class 去重，热路径直接遍历桶并保留完整组合选择器校验。Telemetry schema 3 新增候选计数；新增 `SelectorIndex` 专项及 500/200 工作量门。聚焦 6/6、完整 30/30、Game + Editor Development 与 Cook/Stage/Pak/IoStore 通过。 | M2.2 进入 🚧 4/5；R-01 的 Selector 放大由 100,000 降为 10,694，但全树 Style/Yoga 仍待 M2.3；R-06 增加工作量门证据，宏观 M2 保持 3/7。 |
| 2026-08-12 | 当前 HEAD + working tree | 为全部 52 项 CSS Property 建立唯一元数据表，统一稳定名称、显式继承和六类失效影响；Pseudo State 缓存规则影响并让 Paint-only 路径保留已加载图片及 Brush，避免额外 `LoadObject`。新增元数据完整性/分类测试和真实 Slate Paint 资源安全测试；聚焦 7/7、完整 29/29、Game + Editor Development 与 Cook/Stage/Pak/IoStore 通过。 | M2.2 进入 🚧 3/5；R-04 进入部分缓解，完整 Resource Cache 仍属 M2.4；宏观 M2 保持 3/7。 |
| 2026-08-12 | `a4e4e98` + working tree | Core 为 52 个受支持 CSS 属性建立 append-only ID 与类型化值；规则及 inline style 写入资产版本 4，正常 Runtime 路径不再解析属性名/值字符串，v3 payload 保留一次性 Hydration 回退。两个示例资产由源重编译；工作树文件大小 `HUD 17,010→58,751 B`、`MainMenu 21,827→84,902 B`，仅作 payload 紧凑度后续证据。聚焦 7/7、完整 27/27、Game + Editor Development 与 Cook/Stage/Pak/IoStore 通过。 | M2.2 进入 🚧 2/5；下一项为 Property Metadata；宏观 M2 保持 3/7。 |
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
