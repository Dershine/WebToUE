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

### M2——增量原生运行时 9 / 9

- 可重复七阶段 Telemetry、固定性能 Corpus 与 Observe/Enforce 预算。
- Compiled UI IR、Runtime Instance、Render Data 与 Presentation Cache 分层。
- 类型化 Property/Cascade、Selector Index、有序声明与版本化资产。
- 文档修订级共享 Style Template、Generation-safe Instance Handle 与多 View 隔离。
- Paint-only Pseudo、根 FieldNotify/Text、Persistent Yoga/Resource 的 K=1 局部失效。
- View-owned Display List、空间 Paint/Hit/Scroll 候选与 Slate-compatible batching。
- MainMenu/HUD/ScrollableSettings 的生产宿主、DPI/Safe Zone、输入、Golden 与 `P0.5-if-used` 审计。
- schema 6 Development/Shipping Packaged GT/RT/GPU/input、冷启动、内存和 UMG 对照出口门。
- Win64 Editor Strict、Game Development/Shipping BuildCookRun、Automation 与 Go/No-Go 收口。

### M3 已完成微观路线——宏观 3 / 11

- M3.0：Native Component Registry/Factory/Instance 的显式 C++ 合同、schema 拒绝边界与 RAII 注册所有权。
- M3.1：Screen UI Session 绑定 LocalPlayer/World/Surface/Data/Command/Environment/Clock/Generation；per-LocalPlayer Screen Host、World/LocalPlayer 清理与固定 Packaged Corpus 生产宿主迁移。
- M3.1：Feedback Request/Scope/Input Modality/Correlation/Generation、Null/Recording Router 与失活/旧代次拒绝；真实 Profile/资源/UE 音频后端和作者 Cue 未进入本路线。
- M3.1 `P0.5-if-used`：冻结 MainMenu/HUD/ScrollableSettings 不声明 World Surface，World Host 以自动化 Corpus 证据裁决为 `N/A`。

## 2. 性能测量政策与固定语料

正式可比较环境指纹为 `79E20297`：UE `5.8.1-56057345`、Windows 11 25H2、i7-12700KF（12 核/20 线程）、32 GB、RTX 5050、Win64 WindowsEditor Development。Sampling policy schema `1` 使用 1 次 warmup、20 个计量样本、median P50 和 nearest-rank P95；当前 budget policy schema `9` 分离 Observe/Enforce，只对稳定达标路径启用硬门。

固定 Compile Corpus 为 100 节点/50 规则、500/200 和 2,000/500；Hydration Corpus 为 500/200、2,000/500 和 10,000/500。M2 七阶段为 Hydrate、Style、Measure、Layout、Paint Build、Hit Test、Binding；阶段计时为 inclusive，不得相加作为端到端时长。工作量计数覆盖 Hydrate 节点/规则、Style 节点访问、Selector/Pseudo/Dirty、Yoga build/style write/dirty/result change、Text Layout 构建/计算、Brush、Cache、Binding 字段读取/Op/节点、Resource Manifest/async request/cache hit/failure/cancellation/known-owned bytes、Resource Load Attempt 和 WebToUE 标记分配。Snapshot Telemetry schema `8` 通过 UE 原生 `SetTelemetryStorage` / `AddTelemetryData` 输出到 `Saved/Automation/Telemetry/WebToUEPerformance.csv`；Core/Runtime 不做文件 I/O，capture 热路径不枚举 Telemetry 也不计算分位数。

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
| 2026-08-13 `fbbf74d`～`7f838de` + M2.5 documentation closure | 500/200 单 FieldNotify Observe P50/P95 `0.226002/0.231002 ms`，budget schema `8` Enforce 复测 `0.217000/0.238400 ms`；Binding P95 `0.015600 ms`，Paint Build P95 `0.215100 ms`。K=1 每样本读取 1 根字段、执行 1 Binding Op、更新 1 节点；Style/Selector/Layout/Yoga/Text Cache eviction/Brush/Paint Order/Resource Load Attempt 均为 0，仅重算目标文本 Desired Size 1 次。Warm Layout 与 Unchanged Paint 的 249 个文本均为 Text Key 命中、Desired Size compute 0。环境 `79E20297`，sampling/budget/snapshot schema `1/8/6`。 | FieldNotify 的 Binding、Cascade、Measure/Layout 与节点级 Text Cache 工作量已与直接依赖成正比，时间预算成为硬门；完整 Paint 仍递归生成 Draw Elements，Editor Development 数据不外推为 Packaged GT/RT/GPU 或产品对等。 |
| 2026-08-13 `8578964`～`98676c5` + M2.6 documentation closure | 500 节点暖布局 P50/P95 `0.016801/0.017401 ms`，每个样本 0 Style/Selector、0 Yoga build/write/dirty/result change、0 Text compute、0 Resource Load Attempt；2,000/500 单点 Layout P50/P95 `0.768501/0.913102 ms`，目标 `<16.6 ms` Enforce，K=1 每样本 1 Style visit、104 Selector evaluation、1 Yoga style write、0 Yoga build、3 Layout Result changes、0 Text compute、0 Resource Load Attempt。环境 `79E20297`，sampling/budget/snapshot schema `1/9/8`。 | Persistent Yoga 消除暖布局 Tree 重建；局部 Layout 的 Style/Yoga/Result 工作量可解释并满足压力硬门。数据来自 WindowsEditor Development，只作回归/诊断，不外推 Packaged GT/RT/GPU、RSS/LLM/VRAM 或产品对等。 |
| 2026-08-14 `ca94fe8`、`6b708d5`、`31af5ea` + M2.7 closure | View-owned Display List 以 Instance Handle 持有 Command/Range/Bounds/Clip/Batch Key，局部 Pseudo/Binding/Scroll patch 命令并记录 Dirty Rect；128px 空间网格缩小 Paint/Hit/Scroll 候选，兼容 Rounded Box 复用 LayerId。固定 MainMenu/HUD/ScrollableSettings 在 Development/Shipping 各完成 WebToUE/UMG 120 warmup + 600 sample，12/12 schema 5 result `success=true`、11 次轨迹均观察到 UI 状态变化。K=1 setup hydrate `15/13/21` 节点；WebToUE 测量期 Resource Load Attempt 全为 0，局部 command patch 为 Development `12/20/170`、Shipping `11/20/170`。 | M2.7 的局部 Display/空间候选与真实 Packaged 渲染合同成立；MainMenu WebToUE cold-first-frame `40.948/379.468 ms`（Development/Shipping）中 Shipping 是开放离群，故不宣称 UMG/Gameface 性能对等。Shipping LLM 按 UE 默认配置明确为 not compiled，不把 0 MB 当作已测数据。 |
| 2026-08-14 `8d26643`～`7df6832` + M2.9 closure working tree | Packaged result 升至 schema 6：冷启动分阶段对账、首/第二 View RSS/LLM、Development exact known-owned 与共享 Style Template census、K=1 产品政策；独立 gate schema 1 固定两配置、三 Corpus、WTUE/UMG、120 warmup/600 samples、三次冷启动中位数和 `≤2×`/Batch/Vertex/内存硬门。最终 Development/Shipping gate 均 `success=true`；60/60 Automation 与脚本 Pester 5/5 通过。 | M2.9 达到 ✅ 7/7，宏观 M2 达到 ✅ 9/9；R-01～R-06、R-11～R-14 均为当前 Profile 可接受的 Mitigated。Gameface、Shipping LLM、硬件扫描延迟仍明确为 Unknown/N/A，不被结果伪装为已测；M3 未启动。 |

M2.7 Packaged schema `5/5` 原始矩阵如下；GT/RT/GPU 与 input 均为 `P50/P95/P99 ms`，RSS/LLM 为 600 帧 P50 MiB。Development 原始目录为 `Saved/WebToUEBenchmarks/M2.7/Development/FinalMatrix`，Shipping 为 `Saved/WebToUEBenchmarks/M2.7/Shipping/FinalValidated`；每个 case 的独立子目录保存 `result.json`、`frames.csv` 和 1920×1080 PNG。

| 配置 / Case | Cold ms | GT | RT | GPU | Draw / Batch / Vertices | RSS / LLM MiB | Input→backbuffer-ready |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| Development WebToUE MainMenu | 40.948 | 4.029/5.092/5.716 | 9.700/10.993/11.625 | 8.257/8.838/9.101 | 13 / 12 / 492 | 1746.3 / 2207.0 | 9.417/34.848/34.848 |
| Development UMG MainMenu | 37.996 | 4.117/5.191/5.589 | 9.653/10.985/12.105 | 8.262/8.841/9.111 | 13 / 6 / 492 | 1748.8 / 2208.3 | 9.230/36.766/36.766 |
| Development WebToUE HUD | 37.350 | 4.169/5.251/5.768 | 9.716/11.862/13.368 | 8.212/8.831/9.210 | 5 / 6 / 136 | 1764.7 / 2207.6 | 9.792/33.867/33.867 |
| Development UMG HUD | 29.474 | 4.287/5.352/5.673 | 9.761/11.796/13.117 | 8.191/8.753/9.119 | 5 / 3 / 136 | 1747.4 / 2206.0 | 9.816/34.911/34.911 |
| Development WebToUE ScrollableSettings | 28.412 | 4.316/5.383/5.829 | 9.773/11.854/13.143 | 8.293/8.866/9.091 | 15 / 14 / 384 | 1741.1 / 2205.7 | 9.735/34.612/34.612 |
| Development UMG ScrollableSettings | 32.583 | 4.423/5.462/5.932 | 9.970/12.191/13.601 | 8.268/8.835/9.111 | 18 / 8 / 424 | 1767.8 / 2207.2 | 10.267/35.384/35.384 |
| Shipping WebToUE MainMenu | 379.468 | 3.502/5.011/5.770 | 7.681/9.625/10.375 | 8.076/8.638/8.879 | 13 / 11 / 492 | 1624.7 / N/A | 9.700/53.261/53.261 |
| Shipping UMG MainMenu | 21.146 | 2.499/3.503/4.454 | 4.348/6.490/7.301 | 7.858/8.419/8.607 | 13 / 5 / 492 | 1610.5 / N/A | 8.765/36.552/36.552 |
| Shipping WebToUE HUD | 27.586 | 1.851/2.846/3.439 | 3.099/4.885/5.617 | 7.663/8.274/9.504 | 5 / 5 / 136 | 1552.6 / N/A | 8.257/34.422/34.422 |
| Shipping UMG HUD | 19.740 | 1.560/2.267/2.533 | 2.656/3.820/4.319 | 7.369/8.023/9.194 | 5 / 2 / 136 | 1563.8 / N/A | 8.402/34.425/34.425 |
| Shipping WebToUE ScrollableSettings | 22.168 | 1.630/2.415/2.894 | 2.676/3.961/4.478 | 7.464/8.132/8.798 | 15 / 13 / 384 | 1577.8 / N/A | 7.912/35.455/35.455 |
| Shipping UMG ScrollableSettings | 26.110 | 2.190/2.990/3.677 | 3.487/5.105/5.773 | 7.757/8.375/9.142 | 18 / 7 / 424 | 1559.0 / N/A | 8.600/38.736/38.736 |

所有 case 的 viewport render-target 估算均为 `7.910 MiB`。几何覆盖率 P50（仅 probe child、不是像素级 GPU overdraw）为 Development/Shipping 相同的 WebToUE→UMG：MainMenu `1.2289→1.2376`、HUD `0.0318→0.0284`、ScrollableSettings `1.3264→1.3585`。Shipping `LLM_ENABLED_IN_CONFIG=0`，schema 5 原样写入 `llm_compiled_in=false`、`llm_enabled=false`、`llm_availability=not_compiled_for_configuration`；Development 的 LLM 数据来自启用的 tracker。硬件/RHI 完整到显示的 latency provider 本机未提供，正式可比较字段是脚本输入到最终 GPU backbuffer-ready callback、发生在 present 前，不能称为扫描出像素。

M2.9 的 schema `6` / gate schema `1` 原始目录为 `Saved/WebToUEBenchmarks/M2.9/Development-ExitGate-20260814T1344Z` 与 `Saved/WebToUEBenchmarks/M2.9/Shipping-ExitGate-20260814T1347Z`；根 `gate.json` 保存所有 WTUE/UMG 比较，每个 `Full/<Mode>-<Corpus>` 保存 `result.json`、`frames.csv`、日志与 1920×1080 PNG，`Cold/Trial-02..03` 保存额外独立冷样本。下表只列 WTUE 完整样本与 gate 的三次冷启动中位数；GT/RT/GPU/Input 均为 P95 ms。

| 配置 / Corpus | GT / RT / GPU / Input | Batch / Vertices | WTUE / UMG cold median / ratio | 第二 View RSS / LLM MiB | known-owned ratio |
| --- | --- | --- | --- | --- | --- |
| Development MainMenu | 4.9922 / 10.1633 / 9.1845 / 35.1382 | 12 / 492 | 42.8087 / 37.8076 / 1.1323 | 0.0000 / -1.6150 | 1.0 |
| Development HUD | 5.0603 / 10.6842 / 9.1318 / 35.1073 | 6 / 136 | 40.3818 / 41.8320 / 0.9653 | 0.0000 / -1.7675 | 1.0 |
| Development ScrollableSettings | 4.7092 / 10.3636 / 9.0072 / 34.1510 | 14 / 384 | 41.0683 / 44.5310 / 0.9222 | 0.0117 / -1.8148 | 1.0 |
| Shipping MainMenu | 2.4223 / 4.2872 / 8.8435 / 50.7093 | 11 / 492 | 25.1876 / 24.6958 / 1.0199 | 0.0000 / N/A | N/A |
| Shipping HUD | 2.7175 / 5.3615 / 9.0625 / 39.1234 | 5 / 136 | 24.7016 / 18.3321 / 1.3475 | 0.0000 / N/A | N/A |
| Shipping ScrollableSettings | 2.8172 / 5.1465 / 9.2832 / 39.1355 | 13 / 384 | 24.1321 / 23.0609 / 1.0465 | 0.0000 / N/A | N/A |

六个 WTUE 完整样本均 hydrate 精确 `15/13/21` 个节点、compiled resources 为 0、测量期 Resource Load Attempt/Failure 为 0。每个样本有 10 次测量轨迹；MainMenu/HUD/ScrollableSettings 的 Style visit/Selector evaluation/Binding update/Display patch 为 `15/75/0/12`、`10/10/20/20`、`20/80/0/170`，即每轨迹不超过 `2/8/2`，通过嵌入政策 `4/16/2`。Development WTUE→UMG LLM P50 差值为 `0.184/1.935/-2.006 MiB`，均低于 `64 MiB`；Shipping LLM availability 为 `not_compiled_for_configuration`。独立进程 RSS 差值 Development 为 `+11.059/-14.332/+3.586 MiB`、Shipping 为 `+84.117/+4.266/+8.543 MiB`，因 allocator/driver 基线漂移只报告；同进程第二 View RSS/LLM `≤32 MiB` 与 Development known-owned `≤1.10×` 是实际硬门。三张已人工查看的代表 PNG 为 Development MainMenu、Shipping HUD 和 Shipping ScrollableSettings，均正确显示目标状态；Gameface Release 环境和硬件/RHI scanout provider 仍不可用。

## 4. 发布与工具链证据

- 2026-08-11 的 Development 和 Shipping IoStore 历史验证未发现 WebUI 源文件、Chromium、CEF 或 WebBrowser 运行文件。这是历史阴性证据，发布候选版仍必须重跑 Cook/IoStore/BuildPlugin。
- Editor-only VibeUE 5.0 vendored 提交为 `24ac69d750c1c558a1b78ed5b60644ce000198d3`，版本与归档校验保存于项目根 `Plugins/VibeUE.version.json`。2026-08-11 历史验证覆盖 Win64 BuildPlugin、WebToUEEditor Development 构建、85 个 Agent Skills、83 个 Toolsets、Python API 发现/执行、PerformanceService、13/13 WebToUE 和 17/17 VibeUE 测试；这些数字是当时快照，不代替当前 59/59 WebToUE 回归。
- Editor 生命周期包装器的非沙箱边界、关 Editor 前 Preflight、项目互斥锁、发布宿主/已观察 RunUAT-UBT-Commandlet 进程树 PID 与日志、`Saved/VibeUE/Lifecycle/operation.json` 持久状态和 Pester 6/6 是 R-10 的缓解证据；长期决策见 [ADR-0001](ADRs/ADR-0001-Editor-Lifecycle-Execution-Boundary.md)。M2.7 新增的第 6 项覆盖 `RunUAT.bat` 宿主返回假 0 时解析最终 `AutomationTool exiting with ExitCode=N`，门禁以 AutomationTool 为准。
- 2026-08-12 `b7191ff` + working tree 的 Win64 Development Cook 保存了 576 个包（569 cooked），但 commandlet 因既有 `GameFeatureData` Asset Manager 配置错误及健康 Editor 占用 MCP `127.0.0.1:8000` 而以 ExitCode 25 失败；Stage/Pak/IoStore 未执行，此记录不得视为发布通过。
- 2026-08-12 同一 `b7191ff` + working tree 后续在 `DefaultGame.ini` 添加 UE 5.8 `GameFeatureData` AlwaysCook 规则，并以 `-AdditionalCookerOptions=-ModelContextProtocolPort=8001` 隔离 Commandlet 端口；Win64 Game + Editor Development 构建、Cook（0 errors）、Stage、Pak、IoStore 全部通过。IoStore 写入 569 packages、2,226 chunks，总容器约 `250.11 MiB`；UAT ExitCode 0。对 2,225 条项目 IoStore 文件清单的路径审计未发现 WebUI、Chromium、CEF、WebBrowser、HTML/CSS/JS；Loose Stage 同样无 WebUI/浏览器运行文件，仅有 UE 自带诊断工具 `Engine/Extras/GPUDumpViewer/GPUDumpViewer.html`，不属于 WTUE UI Source 或 Runtime 依赖。
- 2026-08-12 `a4e4e98` + working tree 在安全关闭 Editor 后完成 Win64 Game + Editor Development build、Cook、Stage、Pak、IoStore；569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0。IoStore/UFS/NonUFS 清单的精确路径审计未发现 WebUI、Chromium、CEF、WebBrowser 或 `.html/.css/.js`；发布后 Editor PID 10604 通过 readiness、MCP、Python/World 和聚焦 7/7。
- 2026-08-12 当前 HEAD + working tree 在安全关闭 Editor 后完成 Win64 Game + Editor Development build、Cook（0 errors）、Stage、Pak、IoStore；569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0。首次尝试因健康 Editor 的 Live Coding 门禁在 build 阶段退出，正常关闭后同命令通过；发布后 Editor PID 35244 通过 readiness、MCP 200、Python 和 `Lvl_TopDown` world readiness。
- 2026-08-13 当前 HEAD + working tree 在安全关闭 Editor 后完成 Win64 Game + Editor Development build、Cook（0 errors）、Stage、Pak、IoStore；569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0。发布后 Editor PID 25000 通过 VibeUE 5.0 readiness、MCP 200、UE 5.8.1 Python/`Lvl_TopDown` world readiness 和 `SelectorIndex` 1/1。
- 2026-08-13 `7f838de` + M2.5 documentation closure candidate 通过受跟踪 `SafeBuildCookAndLaunch`：Operation `febf0ea31a3844cdb67f7b84f4144c57`，Win64 Game + Editor Development、Cook（0 errors）、Stage、Pak、IoStore，569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0；完整输出为 `Saved/VibeUE/Lifecycle/Operations/febf0ea31a3844cdb67f7b84f4144c57/uat-stdout.log`，stderr 同目录。发布后 Editor PID `4036` 的 VibeUE 5.0 readiness、MCP 200、UE 5.8.1 Python、`Lvl_TopDown` World 与完整 WebToUE Automation 41/41（8.251 秒）通过。该证据仅证明 Build/Cook/Stage/Pak/IoStore 与 Editor 恢复，不证明 Packaged Runtime 正确性、视觉结果、输入到像素或 GT/RT/GPU 性能。
- 2026-08-13 `98676c5` + M2.6 documentation closure candidate 通过受跟踪 `SafeBuildCookAndLaunch`：Operation `e371bb56384a45b196df8bca7785feca`，Win64 Game + Editor Development、Cook、Stage、Pak、IoStore，569 packages、2,226 chunks、总容器约 `250.11 MiB`，UAT ExitCode 0；完整输出为 `Saved/VibeUE/Lifecycle/Operations/e371bb56384a45b196df8bca7785feca/uat-stdout.log`，stderr 同目录。发布前完整 WebToUE Automation 46/46、零 warning（7.777 秒）；发布后 Editor PID `30796` 的 VibeUE 5.0 readiness、MCP 200、UE 5.8.1 Python、`Lvl_TopDown` World 和零脏包通过。该证据只证明 Build/Cook/Stage/Pak/IoStore 与 Editor 恢复，不证明 Packaged Runtime 正确性、视觉结果、输入到像素、RSS/LLM/VRAM 或 GT/RT/GPU 性能。
- 2026-08-14 M2.7 首次 Shipping 尝试 Operation `790a487b38994ee38d56e323dacba418` 在 Game Shipping 编译 `WebToUEBenchmarkRunner.cpp` 时因无条件引用 Shipping 不存在的 `FLowLevelMemTracker` 失败；UAT 日志明确 `AutomationTool exiting with ExitCode=6`，但旧包装器宿主错误记录 0 并重启到 Healthy。该 operation 明确不计发布通过。Runner 随后按 `ENABLE_LOW_LEVEL_MEM_TRACKER` 守住采集并以 schema 5 记录 compiled/runtime availability；生命周期脚本也改为优先解析 AutomationTool 最终退出标记，Pester 6/6。
- 2026-08-14 修复后 Shipping `SafeBuildCookAndLaunch` Operation `6edff6f79c384e458d226caf2e1b7213` 通过 Win64 Game Shipping Build、Cook、Stage、Pak、IoStore：570 packages、2,227 chunks、总容器 `250.11 MiB`，UAT `BUILD SUCCESSFUL`/ExitCode 0；完整输出在 `Saved/VibeUE/Lifecycle/Operations/6edff6f79c384e458d226caf2e1b7213/uat-stdout.log`。发布后 Editor PID `34056` readiness/MCP 200 通过；该发布门与其后的 Shipping 6/6 Packaged Runtime 矩阵是不同证据，前者不替代后者。
- 2026-08-14 冻结候选先由 `SafeBuildAndLaunch -StrictRebuild` Operation `0eeb10ca92c54e75acdc944169846e21` 完成 UE 5.8 Win64 Editor Development 68 actions、warnings-as-errors，Editor PID `31776` readiness/MCP/`Lvl_TopDown` World 通过；完整 `StartsWith:WebToUE` 为 53/53、0 warnings（9.337 秒），生命周期 Pester 为 6/6。
- 2026-08-14 最终 Development `SafeBuildCookAndLaunch` Operation `1529f9372f814711a796e49dd2e1228e` 通过 Win64 Game Development Build、Cook、Stage、Pak、IoStore：570 packages、2,227 chunks、总容器 `250.11 MiB`，UAT `BUILD SUCCESSFUL`/ExitCode 0；完整输出在 `Saved/VibeUE/Lifecycle/Operations/1529f9372f814711a796e49dd2e1228e/uat-stdout.log`。发布后 Editor PID `30828` readiness/MCP 200、`Lvl_TopDown` World/65 actors 通过；随后 Development schema 5 独立矩阵 6/6 通过，不能由该发布门本身替代。
- 2026-08-14 M2.8 冻结候选由 `SafeBuildAndLaunch -StrictRebuild` Operation `65eae2f1d24c44fc928c866381afb779` 完成 UE 5.8 Win64 Editor Development 71 actions、warnings-as-errors，UBT Succeeded；Editor PID `17432` 的 VibeUE 5.0 readiness/MCP 通过。严格产物上的完整 `StartsWith:WebToUE` 为 59/59、0 warnings、0 skipped（8.186 秒）。
- 2026-08-14 M2.8 因启用 CommonUI/CommonInput 触及 Runtime 模块加载与 Cook 边界，分别执行 tracked `SafeBuildCookAndLaunch` Development Operation `5cac76a275324c9f823a6dca2ef0ca45` 与 Shipping Operation `ccda0164c5364273b09ece159147864e`；两者均为 584 packages、2,250 chunks、总容器 `250.22 MiB`，完成 Game + Editor build、Cook、Stage、Pak、IoStore，UAT `BUILD SUCCESSFUL`/最终 ExitCode 0。Shipping staged manifests 共 2,522 行，包含 CommonUI plugin/config/content，未发现 WebUI、Chromium、CEF、WebBrowser 或 `.html/.css/.js` UI Source 载荷。最终 Editor PID `17316` readiness/MCP、`Lvl_TopDown` World、PIE false、零脏包和完整 Automation 59/59、0 warnings（7.953 秒）通过。该门只证明发布边界和 Editor 恢复，不替代 M2.7 Packaged Runtime 性能或 M2.8 实际 framebuffer/输入证据。
- 2026-08-14 M2.9 冻结源码由 `SafeBuildAndLaunch -StrictRebuild` Operation `4c5dfe0db49344f59e584a9f4fe5778e` 完成 UE 5.8 Win64 Editor Development 70 actions、warnings-as-errors，UBT `Result: Succeeded`；Editor PID `29084` readiness/MCP 通过，严格产物完整 Automation 为 60/60、0 失败/0 跳过（7.630 秒）。
- 2026-08-14 M2.9 最终 Development/Shipping tracked `SafeBuildCookAndLaunch` 分别为 Operation `89c747f8f99c4b61988e16add1532184` 与 `a798956729594f8ea0877017034fb783`；两者均为 584 packages、2,250 chunks、总容器 `250.22 MiB`，完成 Game build、Cook、Stage、Pak、IoStore，AutomationTool 最终 ExitCode 0。Development/Shipping UAT 输出分别位于 `Saved/VibeUE/Lifecycle/Operations/89c747f8f99c4b61988e16add1532184/uat-stdout.log` 与 `Saved/VibeUE/Lifecycle/Operations/a798956729594f8ea0877017034fb783/uat-stdout.log`。最终 Editor PID `17812` readiness/MCP/Python、`Lvl_TopDown` World、PIE false、零脏包和完整 Automation 60/60、0 失败/0 跳过（7.708 秒）通过。发布门只证明 Build/Cook/Stage/Pak/IoStore 与 Editor 恢复；相邻 schema 6 Packaged gate 才提供 Runtime/视觉/性能证据。
- 2026-08-20 M3.1 当前基线 `1fc2a20` + `5342bf6` + `3f1bf20`：主候选先由 `SafeBuildAndLaunch -StrictRebuild` Operation `b4489c5c7ae744d4811c94c669c84448` 完成 UE 5.8 Win64 Editor Development 68 / 68 actions、warnings-as-errors；Packaged 退出生命周期修复后 Operation `7613acbfea8c427e937a08b21f5af0a3` 再完成当前源码 6 / 6 actions。聚焦 Session/ScreenHost/CorpusSurface/PackagedExitPolicy 4 / 4，完整 `StartsWith:WebToUE` 64 / 64、0 failed、0 skipped、0 warnings（8.276 秒）。最终 Development/Shipping tracked BuildCookRun Operations `18966d65178245ed9cb7bb14eb949b6c` / `ba75b45f0803431b8b273dfc58798f14` 均为 584 packages、2,250 chunks、250.22 MiB、AutomationTool ExitCode 0；UAT 输出分别在 `Saved/VibeUE/Lifecycle/Operations/18966d65178245ed9cb7bb14eb949b6c/uat-stdout.log` 与 `Saved/VibeUE/Lifecycle/Operations/ba75b45f0803431b8b273dfc58798f14/uat-stdout.log`。BuildCookRun 只证明 Build/Cook/Stage/Pak/IoStore 与 Editor 恢复；最终 Editor PID `33652` readiness/MCP 健康。
- 2026-08-20 M3.1 真实 Packaged Runtime：Development/Shipping schema 6 gate 根目录分别为 `Saved/WebToUEBenchmarks/M3.1/Development-ExitGate-20260820T0722Z` 与 `Saved/WebToUEBenchmarks/M3.1/Shipping-ExitGate-20260820T0731Z`，根 `gate.json` 均 `success=true`。两配置各 12 / 12 WTUE↔UMG GT/RT/GPU/input 比较通过，3 / 3 Corpus 的三次冷启动中位数比通过，Batch/Vertex、compiled resources=0、K=1 工作量、同进程第二 View、Development LLM≤64 MiB 与 Shipping `not_compiled_for_configuration` 均满足原 schema 6 门。Development WTUE 冷启动中位数 MainMenu/HUD/ScrollableSettings 为 `39.3784/40.5072/41.8434 ms`，对 UMG 比率 `1.3177/1.1398/1.0373`；Shipping 为 `27.7088/25.4965/28.8666 ms`，比率 `1.1194/1.2791/1.0913`。三类 Shipping WTUE PNG 均为 1920×1080 并已人工查看，未见布局、裁剪、文字、焦点或 HUD 退化。首次门在 Runner 写出 `success=true` 后因 Host 到模块卸载阶段才析构而返回进程码 `777003`；`3f1bf20` 在 `RequestExit` 前显式释放 Host/Slate/Strong UObject，修复后完整矩阵退出码与 gate 均恢复为成功。失败目录 `Development-ExitGate-20260820T0710Z` / `0713Z` 保留为诊断证据，不计通过。

## 5. 工程变更记录

只记录会改变工程判断、架构、里程碑或支持边界的变化；普通提交不在此重复 Git 历史。

2026-08-13 的路线复核将后续 M2 重排为 M2.3～M2.9；此前记录中的 M2.3/M2.4/M2.5 编号保留当时语境，不反向改写历史。

| 日期 | 基线 | 变化 | 路线影响 |
| --- | --- | --- | --- |
| 2026-08-20 | `1fc2a20` + `5342bf6` + `3f1bf20` + M3.1 closure | Runtime 新增 `FWebToUESession`、Screen Surface/Environment/Clock/Generation、Feedback Request/Scope 与 Null/Recording Router；`FWebToUEScreenHost` 以 per-LocalPlayer View/SSafeZone、World cleanup/LocalPlayer removal 和显式 Shutdown 管理生命周期，`UWebToUEView` 文档换代推进 Session Generation。固定 WebToUE Packaged Runner 从全局 viewport 注入迁到生产 Host，UMG 对照不变；退出门首次暴露过晚 Host 析构的非零进程码，随后在请求退出前显式释放所有 UI 所有权。`CorpusSurfaceContract` 审计三类冻结 HTML/CSS 不使用 World Surface。 | M3.1 达到 ✅ 8 / 8，宏观 M3 由 `1 / 11` 进入 `3 / 11`；R-17 降为 Medium，R-21 的 Router/Scope/Generation 合同完成但真实 Profile/音频后端仍为 High。World Host 以 `P0.5-if-used=N/A` 关闭当前 Corpus 门；没有新增第三方依赖、资产 schema、VibeUE/Yoga 改动或 P1.0 接口。下一微观路线建议是 M3 Game Thread 更新事务，本次未开始。 |
| 2026-08-20 | M3.0 基线 + UI Feedback route-decision working tree | 项目所有者确认游戏 UI 必须具备音效后，新增统一术语 UI Feedback Cue/Profile/Router 与 [ADR-0005](ADRs/ADR-0005-UI-Feedback-And-Audio-Routing-Boundary.md)：WTUE 只提交事务成功后的语义 Feedback Cue，UI Session 注入 Router，版本化 Profile/项目策略负责 Sound/SoundCue/MetaSound、预取、限频、Scope、用户设置和后端；普通瞬时反馈不经过 Gameplay Command 或 Native Component，长期 BGM/Audio State 仍归游戏 C++。Technical Summary 将 M4 重命名为 Native Presentation/Compositing，并把 Feedback 合同、资源/UE Router、Behavior `EmitFeedbackCue`、Inspector/Trace 分别纳入 M3～M6；新增 R-21。 | 这是产品/架构路线裁决，只修改领域语言、ADR、路线、Evidence Ledger 与 Support Matrix 的未支持说明；没有修改 Runtime、资产 schema、模块依赖或可见输出，也没有运行新的 Automation、Build、Editor、PIE 或 Packaged 门。当前实现事实仍为 M3.0 Native Component 合同完成；新增验收项后 M3 为 `1 / 11`、M4 为 `0 / 9`、M5 为 `0 / 10`，不是实现进度倒退。UI Feedback/音效在 Compiler、Runtime、真实资源和 Packaged 证据完成前继续标记为未支持。 |
| 2026-08-17 | `27df1fa` + route-decision working tree → `66ada22` + `ecb094c` | M3.0 新增项目内实验性的 `FWebToUENativeComponentRegistry`、版本化 Descriptor、类型化 Props/Event/Resource Slot、Factory/Instance 以及 Measure/Pointer+Key Input/Focus/Semantics/Resource/Lifecycle/显式 Slate Widget 合同；Game Thread 注册拒绝无命名空间、零版本、非法 Event/Resource schema 和重复类型，move-only RAII token 只注销其拥有项。首次受保护编译按预期暴露 `FString::ContainsByPredicate` 与 Automation 指针 const 比较错误，修复后最终 `SafeBuildAndLaunch` Operation `db53dab67bed4801bcdc28eed0030fb4` 安全关闭 PID `23732`、归档日志、完成 5 actions 编译/链接，并启动 PID `37040`；写入前置检查全部通过，VibeUE 5.0 readiness 有效，Windows PowerShell 5.1 Status Healthy、MCP 首次即 HTTP 200，Python 输出 UE 5.8.1、Project WebToUE、World ready。最终聚焦 `NativeComponentRegistry` 1 / 1，完整 `StartsWith:WebToUE` 61 / 61、0 failed、0 skipped、7.988 秒。 | M3 达到 🚧 `1 / 10`，R-20 从“无扩展合同”收窄为“合同已建立、Host 尚未挂接”。本路线不接入 UI Source、Compiler、Runtime Tree、UI Session 或 Host，不改变资产版本、Cook、模块依赖、可见输出或热路径；视觉/性能/PIE/Packaged Runtime/BuildCookRun 不适用，不能据此宣称 Native Component 已支持。`P0.5-if-used=N/A`，因为本项自身是无条件 P0.5 前置，具体组件与世界空间仍由后续 Corpus 裁决。 |
| 2026-08-17 | M2.9 closure 基线 + route-decision working tree | 项目所有者在复核脚本行为、UE 材质/纹理、C++ 协议、HUD/世界空间宿主及未显式讨论的系统风险后，接受后续路线重排。新增统一领域术语 Behavior Source/IR、Typed Mutation、UI Session/Surface、UI Command 与 Native Component；[ADR-0004](ADRs/ADR-0004-Compiled-Behavior-And-Native-Interop-Boundary.md) 冻结“无默认通用 JS VM、受限 Behavior TS AOT→原生 Behavior IR、事务提交、原生 Animation Track、类型化 C++ Command 与 Native Component 逃生口”。Technical Summary 将后续正式路线改为 M3 Runtime Semantics/Host/Interop、M4 Native Visual/Compositing、M5 Dynamic UI/Behavior、M6 Modern Authoring/Inspector、M7 Productization，并新增 Behavior 重入、合成层、Session/世界空间、资源驻留、确定性 freshness 和 Native Component 风险。 | 这是产品与架构路线裁决，不修改 Runtime、资产 schema、模块或 Support Matrix，也没有运行新的 Automation、Build、Editor 或 Packaged 门；M2 仍为 `9/9`，M3 为 `0/10`。后续实现必须先以敌意原型冻结事务、时间、属性所有权、多树/Portal、LocalPlayer/Surface、C++ Schema、Resource provenance/freshness 和 Native Component 合同。 |
| 2026-08-14 | `8d26643`、`36fc522`、`e8ac686`、`e524997`、`64c2f7e`、`7df6832` + M2.9 closure | Packaged Runner 升至 schema 6，加入冷启动分阶段归因、第二同文档 View、进程与 exact known-owned 内存证据、嵌入产品政策；新增 `FWebToUEPackagedBenchmarkPolicy` 与 `PackagedExitPolicy` Automation。M2.8 的 `SSafeZone` 宿主使旧 Runner 向根控件派发的合成输入不再到达 Leaf，最终以唯一子 Leaf 解析并直接派发，`CorpusSlateOutput` 同时守住宿主结构、输入处理和实际工作量变化。独立 PowerShell gate 固定两配置 × 三 Corpus × WTUE/UMG，最初 30 帧冷样本只执行 MainMenu 离开步骤而失败，提升为 60 帧并以 Pester 锁定真实 hover 后完整 Development/Shipping 均通过。 | M2.9 与宏观 M2 完成，Go；不引入浏览器/第三方依赖，不修改资产 schema、VibeUE 或 Yoga。冻结 Corpus 的 `P0.5-if-used` 沿用 M2.8 证据为 N/A；BuildPlugin/第二平台/Gameface/Shipping LLM/硬件扫描延迟保持明确后续边界，M3 未启动。 |
| 2026-08-14 | `31c2bc3`、`3331380`、`2ac6e15`、`f0dbaec`、`cfef614`、`03ff4d2` + M2.8 closure | Runtime 新增 Generation-safe Semantic/Focus Node 接口，按 Instance Handle 暴露 ID/Label/Role/Bounds/状态并拒绝旧代次；单 Slate Leaf 以 Tab/空间手柄导航、Accept、焦点 scroll-into-view 和 CommonUI 边界逃逸驱动内部节点。`UWebToUEView` 改由 UE 原生 `SSafeZone` 包裹唯一 Leaf，并保留显式 opt-out。Editor 专项覆盖 HTML/CSS 依赖、成功/失败/恢复重导入、last-good、旧 Handle 失效和 FieldNotify 连续性。三类冻结 Corpus 在 1x/2x 生成六张实际 framebuffer PNG，并以版本化 32×18 RGBA 签名和规范化差异门守住视觉；自动审计确认没有可见滚动条、拖拽/触摸/惯性标记或水平溢出。聚焦 Semantic/Gamepad 2/2、DPI/Corpus 3/3、Reimport/Identity/FieldNotify 3/3、Golden 1/1 和生产宿主集合 4/4 均通过。候选完整套件精确发现 59 项，首次 58/59 暴露旧 Hover benchmark 绕过新宿主直接向根控件派发输入；改走唯一 Leaf 测试钩后聚焦 1/1。 | M2.8 达到 ✅ 4/4，R-14 降为 Mitigated；可见滚动条/基本拖拽按冻结 Corpus 裁决为有证据的 `P0.5-if-used=N/A`，触摸/惯性/完整文本编辑/IME/无障碍适配仍属 P1.0。CommonUI 为项目宿主能力，未引入浏览器、每节点 Widget、资产版本或序列化变化。宏观 M2 保持 8/9，下一路线仅建议 M2.9，本次不进入。 |
| 2026-08-14 | `ca94fe8`、`6b708d5`、`31af5ea` + M2.7 closure | Runtime Presentation 增加 Handle-owned patchable Display Commands、节点/子树 Range、Dirty Rect/Command overlay、128px 空间网格和 large-entry 边界；Paint/Hit/Scroll 先查候选，Rounded Box 以 Slate-compatible key 复用 LayerId。Telemetry schema 12 增加 Display build/patch/reuse/spatial reject、Paint/Hit candidates、batch run/layer merge；新增 5 个 Display/Spatial/Debug/Batch Runtime 专项。Game 模块新增仅在 `-WTUEBenchmark` 时激活的 Packaged Runner 与程序化 UMG 对照，记录真实窗口 renderer/线程/GPU/内存/VRAM/input 数据并保存截图/CSV/JSON；新增 Benchmark 2 项验证冻结 Corpus、K=1 hydrate、sRGB Slate tint 与真实交互。Yoga Game target 改为 precise FP，Hex CSS 颜色按 sRGB→linear 编译，资产版本升至 7，三个 Corpus 由 UI Source 重编译。Shipping LLM 配置边界与生命周期 AutomationTool exit 假阳性均有失败后修复证据。 | M2.7 达到 ✅ 7/7；R-06/R-12 降为 Observe，R-01/R-04 继续 Mitigated，R-11/冷启动留给 M2.9。路线 `P0.5-if-used=N/A`；Gameface Release 环境不可获得，兼容对比 `Unknown`。M2 保持 8/9，下一路线 M2.8 未开始。 |
| 2026-08-13 | `8578964`、`7392b22`、`3df54ab`、`3c8931c`、`98676c5` + M2.6 documentation closure | Runtime Instance 持有 Persistent Yoga Tree；Style change set 只写变化 Yoga 属性，深层 Flex/Wrap/absolute/约束文本依赖专项验证 sibling/ancestor/descendant 结果。资产版本升至 6，编译去重 Texture/Font/String Table Manifest；MainMenu/HUD 从保留 UI Source 重编译。View 创建阶段解析驻留对象并批量异步请求其余路径，Manifest index 作为修订内稳定 Handle，跨 View 共享引擎 UObject；生产 Runtime 的图片/字体路径不再调用 `LoadObject`/`LoadSynchronous`。Schema 8 新增资源生命周期工作量，schema 9 增加 2,000 节点单点 Layout 硬门；新增 `PersistentLayoutState`、`PersistentLayoutDependencies`、`RuntimeStressLayoutBenchmark`、`ResourceManifest`、`ResourceLifecycle`。聚焦布局 5/5、资源 4/4；首次完整套件 44/46 暴露预算 schema 旧预期和异步日志竞态，收紧精确测试契约后完整 46/46、零 warning；补强冷/暖真实 Slate Paint 和取消后恢复后，最终完整套件仍为 46/46、零 warning（7.955 秒）。UE 5.8 Win64 Editor Development 多次增量编译和最终 Game/Editor/Cook/Stage/Pak/IoStore 通过；最终 Editor PID `18040` readiness/MCP/Python/World/零脏包通过。M2.6 不改变视觉设计，viewport/golden 为 N/A；未执行 Packaged Runtime、输入到像素、GT/RT/GPU 或完整 RSS/LLM/VRAM 验证。 | M2.6 达到 ✅ 7/7，宏观 M2 进入 8/9；R-02/R-04/R-13 降为 Mitigated。路线无 `P0.5-if-used` 产品能力，裁决为 `N/A`；Display List/真实渲染属于 M2.7，本次不进入。资源清单/异步句柄边界记录于 ADR-0003。 |
| 2026-08-13 | `fbbf74d`、`b84b2b4`、`99a4405`、`c2635a3`、`09ef231`、`51e0563`、`3af152c`、`7f838de` + M2.5 documentation closure | Compiled UI IR 版本升至 5，编译 Root Field→typed Binding Op→Compiled Node index，Runtime Hydration 转为 Field→Instance Handle 索引；FieldId 只读取变化根属性并更新直接依赖。Text Layout Cache Key 覆盖文本、RichText、字体、颜色/文本样式、Culture 与约束，等尺寸变化跳过 Layout/Yoga，尺寸变化记录文本到根的 Measure/Layout 路径。visible/enabled 使用精确 Style/Pseudo 目标，enabled 在同轮刷新先更新 Disabled state。MainMenu/HUD 从保留 UI Source 重编译并保存版本 5。Snapshot schema 6 显式记录 Resource Load Attempt。新增 `BindingImport`、`BindingIndex`、`FieldNotifyInvalidation`、`TextCacheKeyAndDirtyPath`，并把 Warm Layout/Unchanged Paint Desired Size 复用门改为 0 次计算。首次完整套件因两个旧 249 次预期为 39/41，修正后聚焦 2/2、完整 41/41；资源计数器补强后聚焦 4/4、完整 41/41；发布前和发布后完整套件均 41/41。UE 5.8 Win64 Game + Editor Development、Cook/Stage/Pak/IoStore 通过，最终 Editor PID `4036` readiness/MCP/Python/World 通过。开发中两次编译失败分别由缺少 `Culture.h` 和公开测试接口前置声明导致，均在下一次受跟踪构建修复；资源计数器首次编译因 Telemetry 名称表漏项触发静态断言，补齐后通过。M2.5 不改变既有视觉设计，viewport/golden 为 N/A；未执行 Packaged Runtime、输入到像素或 GT/RT/GPU 验证。 | M2.5 达到 ✅ 6/6，宏观 M2 进入 7/9；R-01 降为 Mitigated，R-06 因 Packaged 渲染/内存证据仍保持开放。M2.5 无 `P0.5-if-used` 产品能力，路线裁决为 `N/A`；嵌套路径/Converter 不进入本路线。版本 5 资产/Cook payload 已由最终 BuildCookRun 验证。 |
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
