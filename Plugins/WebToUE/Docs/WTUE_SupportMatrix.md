# WebToUE 当前支持矩阵

> 文档职责：记录 WTUE Web Subset、绑定、输入、UI Feedback、资源、诊断与资产行为的精确当前边界。
>
> 当前基线：2026-08-20，M4.1 Unreal Texture/Brush Resource Contract 产品闭环。
>
> 2026-08-17 的 M3.0 只建立实验性的 Native Component C++ 注册/实例合同；Native Component 作者声明/Compiler/Runtime 挂接仍未支持。
>
> 2026-08-20 的 M3.1 已实现 Screen UI Session、per-LocalPlayer 代码宿主和 Feedback Request/Null/Recording Router 基础合同；没有作者声明、Compiled Op、Profile、Sound/MetaSound 资源或播放后端，因此“真实 UI 音效”仍未支持。
>
> 2026-08-20 的 M3.2 已实现 Session-owned 更新协调器、非 Game Thread MPSC 入队、遍历来源 Structural Mutation 拒绝、重入/循环预算和 Post-Commit Effect C++ 合同；M3.3 已将 Click 事件监听、状态 Mutation 与 `data-ue-on-click` 默认动作迁入统一事务，现有 FieldNotify 与未来 Behavior 尚未迁入。

> 2026-08-20 的 M3.3 已实现 Generation-safe 事件路径快照、capture/target/bubble/default/stop、Pointer Capture Lost、per Slate User/Pointer 交互身份和聚合 Pseudo 引用计数；专项只证明 C++/Slate 自动化身份隔离，不等同于真实双 LocalPlayer/CommonUI Modal 或 Packaged 多指针验证。
>
> 2026-08-20 的 M3.4 已实现 Game/Unscaled/Real/Test Clock、Virtual Clock、无默认 Tick 的一次性 Timer、异步 Command Result/Timeout/Cancel、worker MPSC、Generation/View/World cleanup 与有界 Trace；这只是 C++ Runtime 前置，不等于类型化 Command Schema、Behavior 或 Animation 已支持。
>
> 2026-08-20 的 M3.5 已实现 Core Property Ownership Policy：canonical Node/CSS/Transform/typed Material Parameter 地址、CSS/Pseudo baseline、唯一 Binding/Behavior durable owner、active-only Animation overlay、visibility/enabled restrictive gate 与稳定 `WTUE-OWN-001..003` 诊断。现有 Binding/CSS/Pseudo 集成已验证；Behavior、Animation Track、Material/MID 与其作者语法仍未支持。
>
> 2026-08-20 的 M3.6 已实现 Runtime Tree Projection Policy：Component/Logical durable ownership、Layout/Paint/Semantic projection、同 Session/Surface Overlay Anchor、Portal cycle/order/Modal scope 与同代 Focus Restore，并提供 `WTUE-TREE-001..005` 诊断。UI Source/Compiler/现有 `SWebToUEView` 尚未创建真实 Portal，不能据此宣称 Overlay/Modal 产品能力已支持。
>
> 2026-08-20 的 M3.7 已实现 Stable Semantic Identity C++ Policy：Component-scoped Stable Key、Route/keyed-path Component Instance Identity、diagnostic-only Source provenance、same-owner/cross-generation 重导入计划、显式状态保留白名单及 `WTUE-ID-001..004` 诊断。UI Source/Compiler/Compiled UI IR/现有 View 尚未生成或消费这些身份，不能据此宣称状态保留式热重载、Component 或 Keyed Diff 已支持。
>
> 2026-08-20 的 M3.8 已实现 C++ Interop Schema Policy：项目 C++ descriptor 是唯一事实源，Core 生成带 Major/Minor 版本的规范 Data/Command snapshot，Editor-only emitter 单向派生确定性 `.d.ts`，并提供 `WTUE-SCHEMA-001..004` 失败门。现有 FieldNotify/View、Command dispatch/payload、Behavior Compiler、MVVM Adapter 与文件 freshness 尚未消费该 snapshot，不能据此宣称类型化作者协议已产品接入。
>
> 2026-08-20 的 M4.1 已让 Unreal Texture/Brush 产品链消费 M3.9 合同：Importer、版本 9 Compiled Asset、Hydration/View、Cook `PreSave`、Development/Shipping Packaged smoke 与可见截图均闭环。relative/generated、Route 作者声明、Material/MID、Feedback 资源和完整 Incremental/DDC/Lockfile 仍未支持。
>
> 工程状态与路线入口：[WTUE_TechnicalSummary.md](WTUE_TechnicalSummary.md)

本文只描述当前承诺，不记录实现历史。支持边界改变时，必须同时更新对应测试、本矩阵和 Technical Summary 的高层能力判断。

## 1. HTML 与 Authoring

标签：

- 文档：`html`、`head`、`body`
- 样式：`style`、`link`
- UI：`div`、`span`、`p`、`img`、`button`
- 行内语义：`strong` / `b`、`em` / `i`、`u`、`br`

解析能力：

- 双引号、单引号、无引号和布尔属性。
- HTML 注释、doctype 跳过和常见/数值实体解码。
- `<style>`、外链 CSS 和元素 `style`。
- 未知标签保留为通用 Flex 容器并产生警告。
- 文本首尾空白裁剪；尚非完整浏览器空白折叠语义。

本地化声明：

- `data-ue-loc-key`
- `data-ue-loc-namespace`
- `data-ue-string-table` + `data-ue-string-key`
- `data-ue-rich-text="true"`

关键产品文案应使用显式 key；无 `id` 节点结构重排可能改变自动作者路径。

## 2. CSS

选择器：类型、Class、ID、复合、后代、直接子代、逗号组，以及 `:hover`、`:active`、`:focus`、`:disabled`。

布局与可见性：

- `display`、`position`、`visibility`、`overflow`
- `width/height`、`min-*`、`max-*`
- `left/top/right/bottom`
- `margin`、`padding` 及四方向属性
- `gap`、`row-gap`、`column-gap`

Flex：

- `flex`、`flex-direction`、`flex-wrap`
- `flex-grow`、`flex-shrink`、`flex-basis`
- `justify-content`、`align-items`、`align-self`

绘制与文本：

- `color`、纯色 `background/background-color`
- `border`、`border-color/width/style/radius`
- `opacity`、`z-index`
- `font-family/size/weight`、`text-align`、`white-space`
- `object-fit: fill/contain/cover`

值：`px`、百分比、零、部分 `auto`；Hex 颜色和少量命名色。尚无 `rgb()`、变量、`calc()`、渐变和完整颜色集合。

声明表示与顺序：当前 52 个受支持属性在编译期映射为稳定 Property ID，并把 keyword、number、integer、length/edges、color、string、flex 和 border 值解析为类型化 payload；同一份 Property Metadata 统一提供稳定名称、继承性及 Style/Measure/Layout/Paint/HitTest/Resource 影响分类。规则和元素 `style` 的正常 Runtime 热路径不再解析属性名或值字符串。有效声明按源顺序进入 Compiled UI IR；固定 Property winner slots 按 inline origin、specificity、source order 和 declaration order 决定胜者，无效声明产生警告且不参与级联。`margin`/`padding` 四边、`gap` 两轴、`flex` 已提供的 grow/shrink/basis、`background` 纯色及 `border` 已提供的 width/color 会与对应 longhand 竞争同一规范槽位。`border: none` 仍按零宽度 lowering；当前不是完整 CSS 四边 border/style reset 模型。

显式继承：`color`、`font-family`、`font-size`、`font-weight`、`text-align`、`white-space`。

## 3. 绑定、事件、输入、反馈与资源

绑定：

| 声明 | 当前行为 |
| --- | --- |
| `data-ue-bind-text="Property"` | 根 UObject 属性转文本 |
| `data-ue-bind-visible="BoolProperty"` | 控制可见性 |
| `data-ue-bind-enabled="BoolProperty"` | 控制可用状态和 `:disabled` |
| FieldNotify | 订阅实际使用的根字段；编译 Root Field→Binding Op→Runtime Instance Handle 直接索引，单字段只读取一次并更新直接依赖节点 |

当前绑定只支持根 UObject 属性；嵌套路径、Converter 和双向绑定仍属 M3。文本绑定以 Instance Handle 保留节点级 Text Layout Cache；Cache Key 覆盖显示文本、RichText 模式、字体、颜色/文本样式、当前 Culture 和换行约束。文本值变化只重算目标文本；Desired Size 相同仅重绘，变化时记录目标 Measure 与文本到根的 Layout 依赖路径。`visible` 只产生 Paint/HitTest 影响；`enabled` 在同一 FieldNotify 刷新内先更新 Disabled Pseudo State，再匹配 `:disabled` 及其编译依赖目标。

属性所有权 C++ 合同：

- 规范地址区分 `node.text`、`node.visibility`、`node.enabled`、canonical CSS computed slot、Visual Transform 与具名且具 Scalar/Vector/Texture 类型的 Material Parameter。CSS shorthand 不是独立地址；`visibility` 归一为 semantic gate。
- Source 与 CSS/Pseudo 是 baseline；CSS/Pseudo 仍在同一 Cascade 内竞争，不存在“Pseudo 自动高于 CSS”的第二优先级。
- Binding 与未来 Behavior 是互斥 durable owner。二者同时 claim 以 `WTUE-OWN-003` 错误拒绝；诊断包含 canonical target 和排序后的 source location，不依赖 claim 遍历顺序。
- 分层属性采用 `Animation(active) > durable owner > CSS/Source`，但 visibility/enabled 采用全部 gate 必须允许的 restrictive composition。Animation 释放时必须显露最新 underlying value，不能写回旧快照。
- 当前只把 Color、BackgroundColor、BorderColor、Opacity、Visual Transform 与 Scalar/Vector Material Parameter 归类为可动画目标；Layout、Text、Visibility、Enabled 和 Texture Parameter 拒绝 Animation writer。Material Parameter 不会由 CSS 名称或任意字符串隐式别名。
- 当前产品路径只有 Binding text/visible/enabled 是已实现 durable owner；`PropertyOwnershipIntegration` 证明 Source text 覆盖、Pseudo visibility gate、Binding visibility gate、Enabled/Pseudo 恢复与上述合同一致。Policy 是 M4/M5 的前置，不等于 Behavior、Animation 或 Material 已支持。

事件：Click 与 Pointer Capture Lost 使用不可变 root→target 事件路径快照；快照携带文档/Session Generation、Slate User/Pointer、Input Modality 与 Correlation，提交前逐段验证父子关系和身份。监听器按 capture→target→bubble 派发，支持 propagation/immediate stop；可取消 Click 支持 prevent default，Pointer Capture Lost 不可取消。`data-ue-on-click="EventName"` 仍广播 `EventName` 和 `ElementId`，但现作为 Session-owned 事务的 Post-Commit 默认动作执行；监听器状态写入先提交，失败/过期事务不广播。当前只有 C++ Listener API 和类型化事件种类，没有 UI Source 事件声明、类型化 payload schema 或 Behavior Event IR。

UI Feedback 基础合同：Runtime 已提供 Feedback Request、`IWebToUEFeedbackRouter`、Null Router 和 Recording Router。Request 固定 Cue/Source/Correlation、Input Modality、Session/LocalPlayer/Viewport/Surface Scope 与 Session Generation；Router 由 UI Session 注入，失活 Session 或旧 Generation 请求明确拒绝。C++ 专项已证明 Feedback 可作为 Session-owned 事务的 Post-Commit Effect，观察全部已提交 Mutation，且失败事务不派发；Click 默认动作也已进入 Post-Commit，但 UI Source 没有 Feedback/Sound 声明，Compiler 不生成 Feedback Cue/Behavior Op，现有事件不会自动生成 Cue。版本化 Profile、UE Sound/SoundCue/MetaSound 资源清单、预取/Cook、限频/去重、用户设置和播放后端仍不存在，因此不能宣称已支持 UI 音效。

更新事务 C++ 边界：每个 `FWebToUESession` 拥有一个 `FWebToUEUpdateCoordinator`。evaluation 在 Game Thread 只收集 State/Structural Mutation 和 Post-Commit Effect，成功后按 State→Structure→Effect 提交；evaluation 拒绝、遍历来源结构写入或预算超限时整笔不提交。非 Game Thread 只能向 MPSC 队列提交 evaluation，由 Game Thread drain；evaluation 重入属于同一原子事务，Commit/Post-Commit 重入进入后续事务。evaluation、Mutation、Effect、单次 drain 与保留 Trace 均有硬上限，Session 失活拒绝新工作并丢弃晚到队列。Click 监听、`data-ue-on-click` 默认动作及 M3.4 Timer/Command terminal evaluation 已接入；该接口仍只是后续 Typed Mutation/Behavior/Command 的基础，现有 FieldNotify 和动态结构尚未迁入。

Clock/异步 C++ 边界：`FWebToUEWorldClock` 明确区分 Game（随 pause 停止、受 dilation）、Unscaled（随 pause 停止、不受 dilation）和 Real（不随 pause 停止、不受 dilation），不提供 Test；`FWebToUEVirtualClock` 为测试/工具独立单调推进四个域。Session-owned `FWebToUEAsyncCoordinator` 提供一次性 Timer 与异步 Command token；Timer 只在显式 `Pump()` 观察 deadline，不注册默认 Tick。worker completion 只入 MPSC，result/timeout exactly-once 并进入更新事务；显式 Cancel、重复/晚到 result、旧 Generation 和失活 Session 均不执行 Mutation。Pending、单次 Pump 与 Trace 有界；文档换代、Host Shutdown、World cleanup 和既有 LocalPlayer removal 路径会取消对应工作。当前虽有类型化 Schema Policy，但异步协调器尚无 Command ID/payload Adapter，Behavior Op、重复 Timer、Animation Track 和 Runtime Inspector 也不存在，因此这些基础合同不应被表述为作者可用的异步行为系统。

C++ Data/Command Schema 边界：`FWebToUEInteropSchemaDescriptor` 是项目 C++ 单一事实源；`FWebToUEInteropSchemaPolicy` 在 Core 中验证闭合的 bool/int32/float/string/name/text/Enum/Record/Array/Optional 值代数、根 Data observability 与 Command request/response/result/cancellable 组合，并生成确定性排序的 Major/Minor snapshot。无效/重复/未知或递归类型/非法 Command shape/不兼容 Minor 演进通过 `WTUE-SCHEMA-001..004` 失败关闭。Editor-only `FWebToUESchemaTypeScriptEmitter` 从 snapshot 生成只读 Data、Enum/Record 与 Command metadata `.d.ts` 文本；声明不是 UHT/UBT 或 UI Compiler 事实源。当前无项目实际 Schema provider、Data/Command Context Adapter、Compiled Binding/Behavior 引用、payload dispatch、MVVM Adapter、磁盘生成或 freshness 接入，因此这是 M5/M6 的 C++ 前置合同，不是现有作者或 Runtime 产品能力。

UI Session / Screen Host：Runtime 已提供 `FWebToUESession` 与代码化 `FWebToUEScreenHost`。Session 绑定 LocalPlayer、World、Screen Surface、Data/Command Context、Environment、显式多域 Clock 与 Generation；一个 Host 拥有一个 `UWebToUEView`，通过 `UGameViewportClient::AddViewportWidgetForPlayer` 附着到对应 LocalPlayer，默认继续使用 `SSafeZone`。显式 Shutdown、World cleanup 或 LocalPlayer removal 均先失活 Session/Async、清除 View 关联，再移除 Slate 内容；文档设置/换代会推进 Session Generation 并同步取消旧代次 Timer/Command。固定 MainMenu/HUD/ScrollableSettings Packaged Runner 已走该生产 Host。C++/Slate 专项已验证多 Slate User/Pointer 的状态隔离，但真实双 LocalPlayer/CommonUI Modal 敌意矩阵尚未执行；World Surface Host 经当前冻结 Corpus 自动审计为 `P0.5-if-used=N/A`，不是已实现能力。

Native Component C++ 边界：Runtime 模块已提供实验性的 `FWebToUENativeComponentRegistry`。注册项必须使用命名空间类型名和非零合同版本，可声明 `UScriptStruct` Props/Event 类型、能力位和带预期 `UClass` 的命名 Resource Slot；Factory/Instance 接口覆盖显式 Slate Widget、Measure、Pointer/Key Input、Focus、Semantic projection、Resource binding 和 Attach/Suspend/Resume/Detach，注册由 Game Thread 上的 move-only RAII token 持有。当前 UI Source 没有 Native Component 声明，Compiler 不生成组件 IR，Runtime Tree/Host 也不会查表或创建实例，因此这只是后续互操作前置合同，不是作者或产品可用能力，且 1.0 外部 API 稳定性尚未承诺。

多树/Portal C++ 边界：`FWebToUETreeProjectionPolicy` 在一个 Session/Surface 和 Runtime UI Instance generation 内分别记录 Component/Logical、Layout、Paint 与 Semantic parent。普通节点可在 Layout/Paint/Semantic projection 中 flatten 非参与 wrapper；Portal root 保留 Component/Logical owner，挂载时成为独立 Layout root，并只把 Paint 或显式 Modal Semantic parent 投影到同 Session/Surface 的 Overlay Anchor。Overlay 顺序由 `OverlayOrder + Instance slot` 决定，跨 Surface/Generation、未知 Anchor、重复挂载和 cycle 均拒绝。最高 Modal 使背景/较低 Modal 的 focus scope inert；关闭后只沿打开前捕获的同代 Logical Semantic candidate chain 恢复焦点，旧 Handle 不通过 id/key 猜测。该 Policy 尚未被 UI Source、Compiler 或现有 Yoga/Display List/Semantic Focus 产品路径消费，真实 Anchor geometry、Clip/Transform/Hit、Portal 输入与视觉/性能仍未实现。

Stable Semantic Identity C++ 边界：`FWebToUESemanticIdentityPolicy` 只以 `(Component Instance Identity, Stable Semantic Key)` 跨 Compiled UI IR 修订匹配节点。Component Instance 由 Route scope、显式 keyed component/list path 与 contract version 标识；provenance 只保存逻辑 source unit/span 并参与诊断，不参与匹配。计划要求同一 Runtime UI Instance Owner、不同 Generation，兼容 Kind/State Contract 后只保留双方显式 `LocalState`/`ScrollIntent`/`FocusIntent` 交集；unkeyed、新增、跨 Component 移动、不兼容和删除均显式重置/退出。旧 Handle、Pointer/Pseudo/Capture、Binding output、Animation、异步工作及 Style/Layout/Paint/Resource cache 不迁移。当前没有 Stable Key/Component 作者声明、Compiler lowering、Compiled IR 字段、真实状态快照/应用或跨重导入 Focus/Scroll 集成；成功重导入仍按现有路径推进 Generation 并重建 View。

Resource Contract：`FWebToUEResourceContractPolicy` 对单个逻辑 Document 验证大小写敏感、非机器绝对路径的 Dependency/Resource/Route/Group ID；provenance 只允许 `/Game`/`/Engine` Unreal Asset、相对 Source 或 `generated:` 输入，并同时指向密封 Source 与 Resource dependency。Dependency 按逻辑 ID/Kind/BLAKE3-256 content hash 规范排序，Compiler fingerprint 与 provenance/residency/version Manifest 分别进入 freshness stamp。空 Route assignment 是 Document fallback；Route 只能把资源提升到同等或更积极的 `Critical`/`Visible`/`Lazy` 等级，不能降级。UI/Resource IR 必须存在，Behavior/Animation/Interop Schema 可显式 `0.0` 缺席；Runtime 只接受相同 Major 且 producer Minor 不高于 consumer 的层。M4.1 Unreal Texture importer 已把当前 HTML/CSS bytes 与 AssetRegistry package-saved content fingerprint 编入 snapshot，版本 9 资产序列化 ResourceId/provenance/residency/层版本/freshness；Hydration 在创建 Runtime Tree 前验证其内部一致性。Cook `PreSave` 复用 importer 构建路径重建 expected stamp，任一 Source/Asset/Compiler/Manifest/version 漂移或 validator 缺席以 `WTUE-RES-004` Error 失败。relative/generated 与 Route 仍只有 Policy，不是作者产品能力；完整 DDC/Lockfile/跨机 Incremental/CI 属 M6。

输入：鼠标移动/点击/滚轮、Tab/Shift+Tab、Enter/Space，以及 Slate `FNavigationEvent` 驱动的手柄 D-pad/空间导航与 Accept。hover/pressed/capture 以稀疏 `(SlateUserIndex, PointerIndex)` 记录，focus 以 Slate User 记录；聚合引用计数使共享节点的 `:hover`/`:active`/`:focus` 在最后一个拥有者离开时才清除，错误 Pointer release 不影响其他身份。Slate capture lost 只清理匹配身份并派发不可取消事件。内部 Generation-safe Semantic/Focus Node 接口暴露 Instance Handle、ID、Label、Role、Bounds、Focusable/Enabled/Visible 状态，并支持 per-user request focus/activate；文档换代后旧 Handle 不再解析。焦点移动到被裁剪的后代时会沿现有滚动路径滚入视野；导航越过首尾边界时返回未处理，使外层 CommonUI/Slate 宿主接管。项目启用 CommonUI/CommonInput，但 WebToUE Runtime 不依赖每节点 CommonUI Widget，也不创建每节点 Slate Widget。尚无触摸/惯性、完整文本编辑/IME 和可访问性适配器；真实双 LocalPlayer/CommonUI Modal 与 Packaged 多指针未验证。

图片：`src` 使用 Unreal 软对象路径，例如 `/Game/UI/T_Logo.T_Logo` 或 `/Engine/EngineResources/DefaultTexture.DefaultTexture`；不支持磁盘图片、HTTP 下载或动态 URL。Texture/Font/String Table Resource Manifest 仍按 `(Kind, Path)` 去重，数组索引只是单个资产修订内稳定的 Manifest Handle；Texture 节点另序列化并消费稳定 ResourceId。`img` 默认 `Visible`，`data-ue-residency="critical|visible|lazy"` 可选 Document 时机：Critical 在 View activation 请求并阻止交互 readiness，Visible 只在节点进入可见布局边界请求，Lazy 只由显式 `UWebToUEView::RequestLazyResource(ResourceId)` 请求；当前没有 Route 作者入口。每个 View 按 ResourceId 持有独立状态、异步 request handle 和 strong resolved UObject；多个 View 共享引擎拥有的 Texture UObject，但不共享 View-owned 状态/handle。生产 Runtime 不调用 `LoadObject` 或 `LoadSynchronous`，状态/Paint 热路径只查 O(1) ResourceId/Handle；解析失败使用确定性无图片 Brush并记录失败，View reset/销毁取消未完成请求。仅影响 Paint 的 Pseudo 和根字段 text/visible/enabled 不发起新资源请求。Development/Shipping Packaged Engine fixture 已证明主 View 与第二 View各 1 次 resident cache hit、0 async/sync load、0 failure/cancellation，并可见绘制真实 Texture Brush；该 K=1 证据不代表大型资源页、Route/释放、Material/PSO/Glyph 或产品级内存/首帧结论。

Runtime 绘制与命中：

- `UWebToUEView` 以 UE 原生 `SSafeZone` 包裹唯一的 `SWebToUEView` Leaf；默认尊重平台 Safe Zone，可显式关闭。Slate 的宿主几何同时承接 UE DPI 与 Safe Zone 缩放，内部逻辑坐标/布局没有第二套设备缩放。
- 一个 `SWebToUEView` Leaf 持有 View-owned、Instance Handle 寻址的 Display List；每个命令记录 Owner、节点/子树 Command Range、Bounds、Visible Bounds、Clip、Depth、交互/滚动状态与 Batch Key。文本命令以 Owner Handle 引用 View-owned Text Layout/Run Cache，不在 Display List 内复制文本或创建独立 Slate Widget。
- Style、Binding、Focus/Pseudo 和 Scroll 的局部变化 patch 对应命令/子树，记录旧/新 Dirty Rect 与 Dirty Command；`WebToUE.Debug.DisplayList=1/2/3` 分别可视化 Dirty Rect、Dirty Command 和全部命令边界。布局或文档结构改变仍可合法重建完整 Display List。
- 128px 空间网格索引 drawable/interactive/scrollable 命令；单命令跨越超过 256 个 Cell 时进入独立 large-entry 列表。Paint 先以 Culling Rect 查候选，Hit Test/Scroll 以点查询候选，再做 Visible Bounds/Clip/Depth 精确判断；该索引只承诺当前固定命令集合的候选缩减，不是 M3 虚拟列表实现。
- 相邻 Rounded Box 只有在 Type、Resource/Shader、Clip、Draw Effect 和圆角/边框几何兼容时才复用 LayerId；颜色不进入 Slate Rounded Box 的几何兼容键。文本和不兼容 Clip/Geometry 会断开 run，保留 Slate 最终 batching 的正确性。
- Packaged benchmark schema `6` 在既有 probe-child Draw Elements/几何覆盖率、全窗口 Slate Batches/Vertices、GT/RT/GPU、RSS、VRAM 与 input-to-backbuffer-ready 上，增加 Asset Load/UI Object Construction/TakeWidget/Prepass/Attach/Renderer Wait 冷启动归因、首/第二 View 进程内存点、Development known-owned Runtime/Presentation 与共享 Style Template census，以及 K=1 Style/Selector/Binding/Resource 工作量政策。`Tools/Invoke-WebToUEPackagedExitGate.ps1` 固定 1920×1080、120 warmup/600 samples、三次冷启动中位数、WTUE/UMG `≤2×`、Batch/Vertex 上限、Development LLM `≤64 MiB` 和同进程第二 View 门。独立进程原始 RSS 只报告；Development 可记录 LLM，UE 默认 Shipping 未编译 LLM 时显式输出 `llm_compiled_in=false`/`not_compiled_for_configuration`，不得把 0 当成已测内存。
- `Tools/Invoke-WebToUEResourceSmoke.ps1` 是独立的 M4.1 Packaged 正确性/视觉门，不改变冻结三页 `maximum_compiled_resources=0` 的 M2 policy；它只接受 `ResourceTextureSmoke` 的 1 个 Resource、主 View恰好 1 次消费、第二 View 1 次 resident cache hit、全阶段 0 sync load/failure/cancellation 与实际 screenshot。它不是 WTUE↔UMG 性能比较门。
- 目标专用 Golden 覆盖 MainMenu/HUD/ScrollableSettings 的 1280×720 逻辑视口，在 1x/2x 分别渲染实际 framebuffer PNG，并以规范化 32×18 RGBA 签名守住跨 DPI 视觉；这是冻结 Corpus 的回归门，不是通用 Screenshot/Golden 工具链。

## 4. 诊断与资产行为

当前诊断覆盖：

- HTML/CSS 文件读取失败。
- 标签名缺失、未匹配闭合标签、未知标签。
- CSS 规则未闭合、声明格式错误。
- 不支持的 at-rule、选择器、属性和值。
- 外链、内联样式的实际文件、行和列。
- 属性所有权的 invalid/untyped target（`WTUE-OWN-001`）、writer 不允许（`WTUE-OWN-002`）与 Binding/Behavior durable owner 冲突（`WTUE-OWN-003`）；后两类未来作者语法接入前主要由 C++ Policy/Automation 使用。
- 树投影的无效/跨代节点域（`WTUE-TREE-001`）、父链/注册顺序（`WTUE-TREE-002`）、Anchor Session/Surface/父投影（`WTUE-TREE-003`）、Portal 挂载/cycle/Modal（`WTUE-TREE-004`）与 Focus Restore token/候选链（`WTUE-TREE-005`）；当前主要由 C++ Policy/Automation 使用。
- Stable Semantic Identity 的无效 Owner/Generation/Component/provenance/node/state domain（`WTUE-ID-001`）、同 Component 重复 Key（`WTUE-ID-002`）、Kind/State Contract 不兼容重置（`WTUE-ID-003`）和 unkeyed retention 请求（`WTUE-ID-004`）；当前只由 C++ Policy/Automation 使用。
- Interop Schema 的无效 Schema/version/identifier（`WTUE-SCHEMA-001`）、UE 大小写语义重复/enum wire value 冲突（`WTUE-SCHEMA-002`）、未知/递归类型与非法 Command shape（`WTUE-SCHEMA-003`）、版本倒退/同版本漂移/Minor breaking evolution（`WTUE-SCHEMA-004`）；当前只由 C++/Editor Policy Automation 使用。
- Resource Contract 的无效 logical ID/provenance/dependency（`WTUE-RES-001`）、residency/assignment（`WTUE-RES-002`）、层版本/IR compatibility（`WTUE-RES-003`）、Cook freshness（`WTUE-RES-004`）和重复/不一致 Manifest（`WTUE-RES-005`）；Unreal Texture Importer/Hydration/View/Cook 已实际消费，relative/generated/Route/Material 仍只到 Policy 边界。

第一次导入错误不会产生有效运行数据；已有资产重导入失败（包括 UI Source 缺失）保留上次成功运行数据并更新诊断。自动化覆盖 HTML/CSS 依赖、成功重导入的 Generation 推进和旧 Handle 失效、失败时 last-good 保留、随后恢复，以及恢复前后 FieldNotify 绑定连续性。

WTUE Document 使用自定义版本 GUID，当前版本 `ResourceConsumerContract`（9）在既有 Compiled Document、本地化富文本、有序/类型化样式、根字段 Binding Op、类型化 Manifest 与 CSS sRGB 颜色之上，增加 Resource Contract manifest/stamp 与 Compiled Node ResourceId consumer。Hex CSS 颜色在编译时由 sRGB 字节转换为 Slate 使用的线性色；低于当前版本的已加载资产请求源文件重编译。版本 3 声明在无法立刻重编译时可于 Hydration 一次性解析兼容 payload，当前 writer 不再写入旧 Name/Value 字符串。项目内 MainMenu/HUD/ScrollableSettings 已从保留 UI Source 持久化为版本 9并继续保持 0 resources；ResourceTextureSmoke 为版本 9、1 个 Engine Texture。全局未加载资产扫描和完整多层字段迁移仍属于 M6。

## 5. 明确尚未支持

- 输入框、文本编辑、IME、表单语义。
- 可见滚动条、基本拖拽、触摸滚动、惯性和虚拟列表。冻结的 MainMenu/HUD/ScrollableSettings 已由自动化审计确认未使用可见滚动条/拖拽/触摸/惯性/水平溢出，因此 M2.8 的对应 `P0.5-if-used` 为有证据的 `N/A`；ScrollableSettings 只使用既有纵向滚轮路径。
- CommonUI 深度组件/Action Router 集成和无障碍适配器；当前只承诺宿主边界协作与内部语义引用接口。
- UI Feedback Cue 的作者声明/语义默认、Compiled UI/Behavior Op、事件到 Cue 的自动映射、Profile、Sound/MetaSound 资源、预取/Cook、限频/去重、Inspector Trace 和真实播放证据；基础 Request、Router 注入、Screen Scope、Generation 拒绝与通用 C++ Post-Commit Effect 已支持。
- World Surface Host、WidgetComponent/RT、3D Feedback Scope、世界输入与独立性能门；当前冻结 Corpus 的裁决是有证据的 `N/A`，不是产品支持。
- Native Component 的 UI Source 声明、Compiler lowering、Compiled IR、Runtime Tree/Host 实例化和真实专用组件；当前只有实验性 C++ Registry/Factory/Instance 合同。
- Portal/Overlay/Anchor 的 UI Source 声明、Compiler lowering、Compiled IR、现有 Runtime Tree/Yoga/Display List/Semantic Focus 挂载、真实 Anchor geometry/Clip/Transform/Hit、视觉/输入/性能与 Packaged 证据；当前只有多树投影与 Focus Restore C++ Policy。
- Stable Semantic Key、Component Instance/provenance 的 UI Source/TSX 声明与 Compiler/IR lowering、状态快照/事务化应用、跨重导入 Focus/Scroll 和状态保留式热重载；当前只有 C++ 匹配/重置规划 Policy。
- 项目实际 C++ Data/Command Schema provider、Data/Command Context 验证 Adapter、Compiled Binding/Behavior/Command payload 接入、MVVM Adapter、`.d.ts` 磁盘生成/freshness 和作者可用类型化协议；当前只有规范 snapshot/version/evolution 与 Editor 内存投影 Policy。
- 嵌套属性路径、Converter、双向绑定、类型化事件载荷。
- 组件、Props、Slots、条件节点、循环和 Keyed Diff。
- Transition、Keyframes、Transform、阴影、渐变、滤镜和 Mask。
- CSS Grid、Table、Float、CSS Variables、`calc()`、媒体查询。
- 独立样式/布局/事件检查器、性能时间线和跨平台矩阵。
