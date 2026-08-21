# ADR-0004：Compiled Behavior 与 UE 原生互操作边界

- 状态：Accepted
- 日期：2026-08-17
- 范围：Behavior Source/IR、动画调度、C++ 数据与命令、Native Component；不表示所有这些能力已经实现

## 背景

WebToUE 的长期目标不是只渲染静态 HTML/CSS，而是让项目以代码完成大部分游戏 UI 的结构、样式、动画和界面行为，同时保留 UE 原生 Cook、输入、材质、资源和平台能力。M2 已建立类型化 Compiled UI IR、Runtime Instance、稳定句柄、持久 Yoga、Display List 和 Packaged 性能合同，但当前没有组件、动态结构、动画或脚本行为。

原路线把 TS/TSX 主要视为构建期作者语法，并把脚本 Runtime 延后为可选决策门。这不能充分表达弹窗、页签、列表、局部状态、异步反馈和动画编排；另一方面，默认引入通用 JavaScript VM 又会扩大到动态语言、GC、DOM/Web API、Cook、安全和平台认证边界。项目需要在两者之间冻结一个可预测的执行合同。

## 决策

1. WebToUE 的默认 Runtime 继续不包含通用 JavaScript VM、DOM 或 Web API；“使用 TS 创作”不等于执行任意 JavaScript。
2. 受限的 **Behavior Source** 进入正式主路线。它使用 WTUE 专用 TypeScript 语法，在 Editor/构建期静态验证并提前编译为带版本的 **Compiled Behavior IR**；Shipping 只消费 IR，不读取或解释 Behavior Source。
3. 原生 Behavior Executor 只处理界面局部状态、受控纯表达式、事件编排、条件/列表所需的 Typed Mutation、动画调度、UI Feedback Cue 以及类型化 UI Command。Gameplay 权威状态、网络权限和实际副作用仍属于游戏 C++；UI Feedback 的播放与资源边界另见 ADR-0005。
4. Behavior 执行采用事务边界：事件求值先收集 Mutation 与 Command，再在安全边界原子提交结构、样式和状态变化；Paint、Layout 或输入遍历过程中不得即时破坏当前树。异步结果、Timer 和完成回调必须携带 UI Session/Instance Generation 与取消合同。
5. 连续动画由原生 Animation IR/Track 和受控 UI Clock 执行。Behavior 只启动、取消、串联或响应动画，不以脚本逐帧计算插值；无活动 Track 时仍恢复零 UI Tick。
6. C++ 协作使用显式版本化的数据与命令 Schema。FieldNotify/直接 C++ 是基础适配，UE MVVM 可作为可选 Adapter；Behavior 不得按字符串任意调用 UObject、UFUNCTION 或游戏世界。
7. WebToUE 替代的是大部分 Widget Blueprint 作者方式和普通逐节点 Widget Tree，不重写 UE 的全部 UI 平台能力。专用输入、视频、小地图、模型预览、CommonUI 或项目控件通过受控 **Native Component Registry** 暴露类型化 Props、Events、Measure、Focus、Input、Semantics、Resources 和 Lifecycle。
8. 单 Slate Leaf 仍是普通 WTUE 内容的默认高性能路径；Native Component、Portal、世界空间或高级合成若需要不同宿主形态，必须通过显式 Surface/Host 合同进入，不能成为隐式每节点 UWidget 回退。
9. Source Map 必须能把 Behavior Op、Animation Track、Compiled Node、Runtime Handle 和 Paint Command 追溯到作者源码；Compiler/IR/Schema 版本与能力位必须进入确定性构建和 Cook freshness 合同。
10. 若冻结 Corpus 将来证明受限 Behavior IR 不足，可以另立 ADR 评估独立、按资产 Opt-in 的脚本模块。该模块不得成为静态资产的默认依赖，也不得把 JavaScript 动画 Tick 或任意 UObject 访问带入 Core 路径。

## M3.0 Native Component 合同细化

- 注册键是带命名空间的 `FName TypeName` 加非零 `ContractVersion`；同名类型不能隐式覆盖，注册只在 Game Thread 发生，并由 move-only RAII token 解除，避免模块卸载后的悬挂 Factory。
- Descriptor 以能力位声明 Measure、Pointer Input、Key Input、Focus、Semantics 和 Resources；Props 与 Event Payload 由 `UScriptStruct` 标识，Resource Slot 由稳定名称、预期 `UClass` 和 required 标记定义。字符串只用于已注册 schema 的名字，不允许据此任意反射调用 UObject/UFUNCTION。
- Factory 为一个已验证注册创建实例；实例显式提供 Slate Widget、Props 应用、约束测量、输入、焦点、Semantic projection、资源绑定，以及 Attach/Suspend/Resume/Detach 生命周期。资源对象和 Payload 内存由 Host 拥有，实例只在调用期间借用；异步工作必须在后续 UI Session/Generation 合同建立后才能进入产品路径。
- 普通 WTUE 节点不查询 Registry、不创建该接口，也不改变单 Slate Leaf 热路径。M3.0 没有定义 UI Source 语法、Compiled Component IR、UI Session、Host 挂接、Portal、多树身份或世界空间行为；这些必须由后续验收项各自实现和验证。
- 此头文件是当前项目内实验性接口；M7 之前不承诺外部 ABI/API 稳定性、第三方插件兼容等级或独立分发合同。

## 被否决的替代方案

- **继续完全没有界面行为层**：可以保持 Runtime 最小，但复杂 UI 会把所有局部表现状态和动画编排推回 C++，无法兑现代码式前端作者体验。
- **只允许动画 TS**：适合作为实现切片，但无法覆盖条件、列表、弹层、局部状态和异步反馈，最终会产生另一套互不兼容的指令系统。
- **默认接入通用 JavaScript VM**：实现任意 TS 的表面入口，但会引入动态语言语义、GC、启动/内存、安全、平台认证和 Web 生态兼容预期；单独 VM 也不提供 DOM、ReactDOM 或浏览器事件语义。
- **让 Behavior 直接反射调用任意 UObject/UFUNCTION**：接入容易，但破坏类型、权限、Cook、多人权威和可审计边界。
- **在 WTUE Core 中重写所有原生控件**：会复制 Slate/UMG/CommonUI 的平台工作，并使文本输入、媒体、VR 和项目专用控件无限扩大核心范围。

## 结果与约束

- 后续路线必须先冻结 UI Session、事务、时间、事件、属性所有权、异步取消、Portal/多树映射和 Native Component 合同，再扩张动画和 Behavior 语法。
- Material、Transform、Mask 或 Filter 必须区分普通 Brush、子树合成层和世界空间 Surface；允许 UE Material 不等于承诺浏览器级合成器。
- UI Feedback 只作为事务提交后的语义意图进入 Behavior/Session，不把具体声音资产、播放 API、长期音乐状态或项目音频策略写入 Behavior IR；详细边界见 [ADR-0005](ADR-0005-UI-Feedback-And-Audio-Routing-Boundary.md)。
- TS/TSX 编译器必须静态处理受限 AST，不默认执行任意组件函数、npm lifecycle script 或第三方构建代码。
- Support Matrix 只按已取得的实现与证据更新：M3.0 可记录实验性 Native Component C++ Registry/Factory/Instance 合同，但在 UI Source、Compiler、Host/Runtime 实例化、真实组件、适用的 Packaged 性能与文档门全部通过前，不得宣称 Native Component 为产品可用能力；Behavior Source、Material 参数作者语法与动态 Material Animation 仍保持未支持，静态 Material Brush 与 C++ typed Scalar/Vector submission 以 M4.3 产品证据为准。
