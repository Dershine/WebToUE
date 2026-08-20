# WebToUE 当前支持矩阵

> 文档职责：记录 WTUE Web Subset、绑定、输入、UI Feedback、资源、诊断与资产行为的精确当前边界。
>
> 当前基线：2026-08-20，M3.6 多树、Portal 与 Focus Restore 边界。
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

Clock/异步 C++ 边界：`FWebToUEWorldClock` 明确区分 Game（随 pause 停止、受 dilation）、Unscaled（随 pause 停止、不受 dilation）和 Real（不随 pause 停止、不受 dilation），不提供 Test；`FWebToUEVirtualClock` 为测试/工具独立单调推进四个域。Session-owned `FWebToUEAsyncCoordinator` 提供一次性 Timer 与异步 Command token；Timer 只在显式 `Pump()` 观察 deadline，不注册默认 Tick。worker completion 只入 MPSC，result/timeout exactly-once 并进入更新事务；显式 Cancel、重复/晚到 result、旧 Generation 和失活 Session 均不执行 Mutation。Pending、单次 Pump 与 Trace 有界；文档换代、Host Shutdown、World cleanup 和既有 LocalPlayer removal 路径会取消对应工作。当前没有类型化 Command Schema/payload、Behavior Op、重复 Timer、Animation Track 或 Runtime Inspector，因此这些基础合同不应被表述为作者可用的异步行为系统。

UI Session / Screen Host：Runtime 已提供 `FWebToUESession` 与代码化 `FWebToUEScreenHost`。Session 绑定 LocalPlayer、World、Screen Surface、Data/Command Context、Environment、显式多域 Clock 与 Generation；一个 Host 拥有一个 `UWebToUEView`，通过 `UGameViewportClient::AddViewportWidgetForPlayer` 附着到对应 LocalPlayer，默认继续使用 `SSafeZone`。显式 Shutdown、World cleanup 或 LocalPlayer removal 均先失活 Session/Async、清除 View 关联，再移除 Slate 内容；文档设置/换代会推进 Session Generation 并同步取消旧代次 Timer/Command。固定 MainMenu/HUD/ScrollableSettings Packaged Runner 已走该生产 Host。C++/Slate 专项已验证多 Slate User/Pointer 的状态隔离，但真实双 LocalPlayer/CommonUI Modal 敌意矩阵尚未执行；World Surface Host 经当前冻结 Corpus 自动审计为 `P0.5-if-used=N/A`，不是已实现能力。

Native Component C++ 边界：Runtime 模块已提供实验性的 `FWebToUENativeComponentRegistry`。注册项必须使用命名空间类型名和非零合同版本，可声明 `UScriptStruct` Props/Event 类型、能力位和带预期 `UClass` 的命名 Resource Slot；Factory/Instance 接口覆盖显式 Slate Widget、Measure、Pointer/Key Input、Focus、Semantic projection、Resource binding 和 Attach/Suspend/Resume/Detach，注册由 Game Thread 上的 move-only RAII token 持有。当前 UI Source 没有 Native Component 声明，Compiler 不生成组件 IR，Runtime Tree/Host 也不会查表或创建实例，因此这只是后续互操作前置合同，不是作者或产品可用能力，且 1.0 外部 API 稳定性尚未承诺。

多树/Portal C++ 边界：`FWebToUETreeProjectionPolicy` 在一个 Session/Surface 和 Runtime UI Instance generation 内分别记录 Component/Logical、Layout、Paint 与 Semantic parent。普通节点可在 Layout/Paint/Semantic projection 中 flatten 非参与 wrapper；Portal root 保留 Component/Logical owner，挂载时成为独立 Layout root，并只把 Paint 或显式 Modal Semantic parent 投影到同 Session/Surface 的 Overlay Anchor。Overlay 顺序由 `OverlayOrder + Instance slot` 决定，跨 Surface/Generation、未知 Anchor、重复挂载和 cycle 均拒绝。最高 Modal 使背景/较低 Modal 的 focus scope inert；关闭后只沿打开前捕获的同代 Logical Semantic candidate chain 恢复焦点，旧 Handle 不通过 id/key 猜测。该 Policy 尚未被 UI Source、Compiler 或现有 Yoga/Display List/Semantic Focus 产品路径消费，真实 Anchor geometry、Clip/Transform/Hit、Portal 输入与视觉/性能仍未实现。

输入：鼠标移动/点击/滚轮、Tab/Shift+Tab、Enter/Space，以及 Slate `FNavigationEvent` 驱动的手柄 D-pad/空间导航与 Accept。hover/pressed/capture 以稀疏 `(SlateUserIndex, PointerIndex)` 记录，focus 以 Slate User 记录；聚合引用计数使共享节点的 `:hover`/`:active`/`:focus` 在最后一个拥有者离开时才清除，错误 Pointer release 不影响其他身份。Slate capture lost 只清理匹配身份并派发不可取消事件。内部 Generation-safe Semantic/Focus Node 接口暴露 Instance Handle、ID、Label、Role、Bounds、Focusable/Enabled/Visible 状态，并支持 per-user request focus/activate；文档换代后旧 Handle 不再解析。焦点移动到被裁剪的后代时会沿现有滚动路径滚入视野；导航越过首尾边界时返回未处理，使外层 CommonUI/Slate 宿主接管。项目启用 CommonUI/CommonInput，但 WebToUE Runtime 不依赖每节点 CommonUI Widget，也不创建每节点 Slate Widget。尚无触摸/惯性、完整文本编辑/IME 和可访问性适配器；真实双 LocalPlayer/CommonUI Modal 与 Packaged 多指针未验证。

图片：`src` 使用 Unreal 软对象路径，例如 `/Game/UI/T_Logo.T_Logo`；不支持磁盘图片和 HTTP 下载。编译资产生成 Texture/Font/String Table 类型化 Resource Manifest，并按 `(Kind, Path)` 去重；清单数组索引是单个资产修订内的稳定资源 Handle。每个 View 按清单建立强 UObject 槽位：已驻留对象直接解析，未驻留路径在 View 创建/Resource 重建边界批量异步请求，完成后以弱 Slate 引用触发失效；多个 View 共享引擎拥有的 UObject，但不共享 View-owned 请求/句柄数组。Presentation、文本与状态更新只查稳定槽位，生产 Runtime 不调用 `LoadObject` 或 `LoadSynchronous`；解析失败使用无图片 Brush/默认字体并记录失败，重置或销毁 View 取消未完成请求。仅影响 Paint 的 Pseudo State 变化仍只更新受影响目标并保留无关 Brush、Text Cache 与 Paint Order；根字段 text/visible/enabled 绑定不会发起资源请求。网络、磁盘文件、动态 URL、重试/下载策略和资源流送优先级不在当前边界。

Runtime 绘制与命中：

- `UWebToUEView` 以 UE 原生 `SSafeZone` 包裹唯一的 `SWebToUEView` Leaf；默认尊重平台 Safe Zone，可显式关闭。Slate 的宿主几何同时承接 UE DPI 与 Safe Zone 缩放，内部逻辑坐标/布局没有第二套设备缩放。
- 一个 `SWebToUEView` Leaf 持有 View-owned、Instance Handle 寻址的 Display List；每个命令记录 Owner、节点/子树 Command Range、Bounds、Visible Bounds、Clip、Depth、交互/滚动状态与 Batch Key。文本命令以 Owner Handle 引用 View-owned Text Layout/Run Cache，不在 Display List 内复制文本或创建独立 Slate Widget。
- Style、Binding、Focus/Pseudo 和 Scroll 的局部变化 patch 对应命令/子树，记录旧/新 Dirty Rect 与 Dirty Command；`WebToUE.Debug.DisplayList=1/2/3` 分别可视化 Dirty Rect、Dirty Command 和全部命令边界。布局或文档结构改变仍可合法重建完整 Display List。
- 128px 空间网格索引 drawable/interactive/scrollable 命令；单命令跨越超过 256 个 Cell 时进入独立 large-entry 列表。Paint 先以 Culling Rect 查候选，Hit Test/Scroll 以点查询候选，再做 Visible Bounds/Clip/Depth 精确判断；该索引只承诺当前固定命令集合的候选缩减，不是 M3 虚拟列表实现。
- 相邻 Rounded Box 只有在 Type、Resource/Shader、Clip、Draw Effect 和圆角/边框几何兼容时才复用 LayerId；颜色不进入 Slate Rounded Box 的几何兼容键。文本和不兼容 Clip/Geometry 会断开 run，保留 Slate 最终 batching 的正确性。
- Packaged benchmark schema `6` 在既有 probe-child Draw Elements/几何覆盖率、全窗口 Slate Batches/Vertices、GT/RT/GPU、RSS、VRAM 与 input-to-backbuffer-ready 上，增加 Asset Load/UI Object Construction/TakeWidget/Prepass/Attach/Renderer Wait 冷启动归因、首/第二 View 进程内存点、Development known-owned Runtime/Presentation 与共享 Style Template census，以及 K=1 Style/Selector/Binding/Resource 工作量政策。`Tools/Invoke-WebToUEPackagedExitGate.ps1` 固定 1920×1080、120 warmup/600 samples、三次冷启动中位数、WTUE/UMG `≤2×`、Batch/Vertex 上限、Development LLM `≤64 MiB` 和同进程第二 View 门。独立进程原始 RSS 只报告；Development 可记录 LLM，UE 默认 Shipping 未编译 LLM 时显式输出 `llm_compiled_in=false`/`not_compiled_for_configuration`，不得把 0 当成已测内存。
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

第一次导入错误不会产生有效运行数据；已有资产重导入失败（包括 UI Source 缺失）保留上次成功运行数据并更新诊断。自动化覆盖 HTML/CSS 依赖、成功重导入的 Generation 推进和旧 Handle 失效、失败时 last-good 保留、随后恢复，以及恢复前后 FieldNotify 绑定连续性。

WTUE Document 使用自定义版本 GUID，当前版本 `CssSrgbColors`（7）包含初始 Compiled Document、本地化富文本、有序声明、类型化样式声明、根字段 Binding Op、类型化 Resource Manifest 和 CSS sRGB 颜色演进。Hex CSS 颜色在编译时由 sRGB 字节转换为 Slate 使用的线性色；已加载旧资产会请求源文件重编译。版本 3 声明在无法立刻重编译时可于 Hydration 一次性解析兼容 payload，版本 4～7 writer 不再写入旧 Name/Value 字符串。项目内 MainMenu/HUD/ScrollableSettings 已从保留的 UI Source 重编译为版本 7；全局未加载资产扫描和完整字段级迁移仍属于 M6。

## 5. 明确尚未支持

- 输入框、文本编辑、IME、表单语义。
- 可见滚动条、基本拖拽、触摸滚动、惯性和虚拟列表。冻结的 MainMenu/HUD/ScrollableSettings 已由自动化审计确认未使用可见滚动条/拖拽/触摸/惯性/水平溢出，因此 M2.8 的对应 `P0.5-if-used` 为有证据的 `N/A`；ScrollableSettings 只使用既有纵向滚轮路径。
- CommonUI 深度组件/Action Router 集成和无障碍适配器；当前只承诺宿主边界协作与内部语义引用接口。
- UI Feedback Cue 的作者声明/语义默认、Compiled UI/Behavior Op、事件到 Cue 的自动映射、Profile、Sound/MetaSound 资源、预取/Cook、限频/去重、Inspector Trace 和真实播放证据；基础 Request、Router 注入、Screen Scope、Generation 拒绝与通用 C++ Post-Commit Effect 已支持。
- World Surface Host、WidgetComponent/RT、3D Feedback Scope、世界输入与独立性能门；当前冻结 Corpus 的裁决是有证据的 `N/A`，不是产品支持。
- Native Component 的 UI Source 声明、Compiler lowering、Compiled IR、Runtime Tree/Host 实例化和真实专用组件；当前只有实验性 C++ Registry/Factory/Instance 合同。
- Portal/Overlay/Anchor 的 UI Source 声明、Compiler lowering、Compiled IR、现有 Runtime Tree/Yoga/Display List/Semantic Focus 挂载、真实 Anchor geometry/Clip/Transform/Hit、视觉/输入/性能与 Packaged 证据；当前只有多树投影与 Focus Restore C++ Policy。
- 嵌套属性路径、Converter、双向绑定、类型化事件载荷。
- 组件、Props、Slots、条件节点、循环和 Keyed Diff。
- Transition、Keyframes、Transform、阴影、渐变、滤镜和 Mask。
- CSS Grid、Table、Float、CSS Variables、`calc()`、媒体查询。
- 独立样式/布局/事件检查器、性能时间线和跨平台矩阵。
