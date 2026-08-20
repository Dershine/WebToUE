# ADR-0005：UI Feedback 与音频路由边界

- 状态：Accepted
- 日期：2026-08-20
- 范围：UI Feedback Cue、事务派发、UI Session/Router、反馈资源与音频后端；不表示这些能力已经实现

## 背景

游戏 UI 需要 Hover/Focus、Navigate、Press、Confirm、Cancel、Error、Panel、Tab、Slider 等即时反馈，但 WebToUE 既不能把具体 Sound 路径和 `PlaySound` 调用散落到节点/Behavior 中，也不应复制 UE Audio、MetaSound、项目 Audio Director 或第三方音频中间件。直接播放还会在 Hover→Focus、手柄导航、快速滑块、事件重入、双 LocalPlayer、世界空间 Surface 和 Session 销毁时产生重复、错位、延迟或错误生命周期。

WebToUE 已在 ADR-0004 中选择事件驱动的 Behavior IR、事务提交、UI Session、类型化 UI Command 和 Native Component 边界。UI 反馈必须进入这套语义，而不是形成与动画、Command 和资源系统并行的临时播放通道。

## 决策

1. WebToUE Core 表达 **UI Feedback Cue**，不表达“播放某个声音”。Cue 使用编译期验证、带命名空间的稳定语义 ID；Compiled UI/Behavior IR 不保存任意运行时文件路径、任意 UObject 调用或项目音频函数名。
2. 标准语义控件可以由交互角色/事件获得默认 Cue，组件或主题可以覆盖；复杂编排由 Behavior 发出受控 `EmitFeedbackCue` Op。具体 HTML/TSX/Behavior 语法在 M5 冻结，不因本 ADR 被视为已支持。
3. Feedback 是 UI 事务的 Post-Commit Effect：事件求值只收集请求，结构/状态提交成功后才派发；失败、回滚或被预算拒绝的事务不发声。请求携带 Event Correlation、UI Session/Generation、来源语义身份和输入模态，支持去重、追踪和过期拒绝。
4. **UI Feedback Router** 由 Host 注入 UI Session。WTUE Runtime 只提交类型化请求；Router 根据 Local Player、Slate User、Viewport、UI Surface、主题、用户设置和项目策略执行。默认实现可以由 UE Subsystem 承载，但 Subsystem 单例不是 Core 协议。
5. **UI Feedback Profile** 是版本化的 Cue→表现策略/资源映射。0.5 必须支持 UE 原生 2D UI Sound/SoundCue/MetaSound 路径；项目可以提供自定义 Router 接入 Audio Director 或中间件，WebToUE Core 不强依赖第三方音频插件。`FSlateSound` 可以是简单后端适配，不是公共领域模型。
6. Profile/Router 拥有音量路由、SoundClass/Submix、Concurrency、Voice Limit、Cooldown/Throttle、随机 Variant、暂停行为和用户静音设置。Behavior 不携带这些播放策略；高频 Slider/Scroll/Navigate 与 Hover→Focus 去重由输入模态和 Correlation-aware Policy 处理。
7. Feedback 请求显式区分 Session、LocalPlayer、Viewport Global 与 Surface Scope。Screen Surface 通常解析为 2D；World Surface 是否空间化、使用哪个位置/衰减由 Host/Profile 决定，Behavior 不提交裸世界坐标。
8. Critical Feedback 资源随 Profile/Route/UI Session 预取并进入 Resource provenance、Cook dependency、chunk、freshness 和驻留合同；常用 Confirm/Cancel/Navigate 不允许在首次交互时才同步加载。若资源未就绪，Router 按可观测策略丢弃或降级，不延迟原交互事务。
9. 瞬时 UI Feedback 不经过 Gameplay UI Command 往返。长期 BGM、菜单音乐、Audio State、联网/购买/保存结果等项目权威行为仍由类型化 UI Command 请求游戏 C++/Audio Director；“按下”“已接受”“已拒绝”是不同 Cue，不能把乐观 Press 当成权威成功。
10. 一次性 Cue 派发后不依赖来源节点继续存活；Loop/持续反馈若未来进入范围，必须返回由 UI Session/Generation 拥有的显式 Playback Token，并另行冻结 Stop/Fade/Detach 合同。普通反馈不得借 Native Component 或每节点 Widget 获取生命周期。
11. 屏幕阅读器语音、对白、音乐状态与 Gameplay 空间音频不属于普通 UI Feedback。UI Feedback 可以为未来的本地震动或其他表现保留扩展能力，但 0.5 不因此承诺通用多模态反馈框架。
12. Runtime 必须提供 Null/Recording Router 与确定性 Feedback Trace；测试覆盖 Hover/Focus 双触发、快速滑块限频、同事件重入、双 LocalPlayer、Session 销毁、异步 Command Result、Profile 缺项、资源未驻留、Screen 2D 与 World Surface 策略。日志区分“请求、提交、去重、限频、路由、缺失和丢弃”，不把扬声器实际出声伪装成确定性可测事实。

## 被否决的替代方案

- **节点直接引用 Sound 并调用播放 API**：实现快，但把主题、用户设置、预载、并发、输入模态、分屏和音频后端耦合进 UI Source。
- **所有 UI 音效都转为 UI Command**：会污染 Gameplay 协议、增加往返和延迟，并使普通本地反馈依赖游戏权威层；只保留给长期 Audio State 与真实业务结果。
- **把音效作为 Native Component 能力**：会让跨节点、跨 Session 的表现策略错误地落入局部控件生命周期，并鼓励每节点 Widget 回退。
- **模拟 HTML `<audio>` 或 Web Audio API**：扩大到媒体、流、图节点、网页生命周期和平台权限，不符合 WTUE Web Subset 与原生 UE 音频复用目标。
- **强制固定 GameplayTag、Subsystem 或单一音频中间件**：会把项目策略固化为 Core 依赖；稳定 Cue ID 与注入 Router 已能提供类型化边界。

## 结果与路线约束

- M3 必须在 UI Session/事务路线中冻结 Feedback Request、Router 注入、Post-Commit、Scope、Correlation、Generation 和 Null/Recording 边界。
- 原生声音资源、Profile、预取/Cook、默认 UE Router、输入模态去重、限频和 Screen/World 策略进入 M4“UE 原生表现与合成”。
- Behavior 的 `EmitFeedbackCue`、语义默认/覆盖及确定性敌意 Corpus 进入 M5；Source Map、Preview/Inspector 与 Feedback Trace 进入 M6。
- Support Matrix 在 Compiler、Runtime、真实 UE 资源、Packaged 交互和诊断证据完成前必须继续标记 UI Feedback/音效为未支持。
