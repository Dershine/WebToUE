# ADR-0007：多树、Portal 与 Focus Restore 边界

- 状态：Accepted
- 日期：2026-08-20
- 范围：Runtime UI Instance 内的 Component/Logical、Layout、Paint/Compositing 与 Semantic Tree 投影，以及 Portal/Overlay Anchor/Modal/Focus Restore 合同；不表示 Portal 作者语法、Compiler lowering 或真实合成已实现

## 背景

当前 Runtime 将编译节点水合为一棵 Parent/Children 树，Yoga、事件路径、Paint Order、Display List 与 Semantic Focus 都从该树派生。普通静态界面可以共享这条父链；Overlay、Tooltip、Dropdown、Modal 和后续 Native Component/动态组件却不能把“谁拥有状态和事件”与“在哪里布局、绘制和暴露语义”继续视为同一关系。

若 Portal 通过直接重挂 Runtime Node 实现，Click capture/bubble、继承、Pseudo 祖先、Binding/Behavior 所有权和组件生命周期会随视觉层级改变；若仅在 Paint 时临时跳转，Layout、Clip、Hit Test、Semantic Bounds、焦点约束和关闭后的焦点恢复又没有可验证合同。跨 LocalPlayer、Surface 或 Generation 的 Overlay 还可能把输入和旧节点句柄泄漏到错误的 UI Session。

M3.6 需要先冻结投影与失败边界，供 M4 的 Transform/Clip/Compositing 和 M5 的动态结构消费；本路线不提前实现作者 Portal、Stable Semantic Key 或具体表现。

## 决策

1. 一个 Runtime UI Instance generation 内只有一份节点所有权。每个节点继续由 `FWebToUEInstanceHandle` 标识，不因 Portal 复制节点、创建第二份 Runtime State，或默认创建 UObject/UWidget/Slate Widget。
2. **Component Tree** 表达组件实例、Props/Slots、局部状态与生命周期所有权；**Logical Tree** 表达 UI 事件路径、继承/Cascade 祖先、Binding/Behavior 作用域和结构 Mutation 的语义父子关系。两者是 durable ownership，不由视觉挂载点改写。
3. **Layout Tree** 只表达 Measure/Constraint/Yoga 依赖；**Paint/Compositing Tree** 表达绘制顺序、Clip/Transform/Layer/Hit-Test 投影；**Semantic Tree** 表达可访问语义、导航与 Modal focus scope。三者均从同一节点所有权投影，可跳过不参与该树的包装节点，但不能生成新的 durable owner。
4. 普通节点默认把 Logical parent 投影为 Layout/Paint/Semantic parent；不参与某棵投影树的包装节点在该树中被 flatten 到最近参与的 Logical ancestor。Component parent 可以与 Logical parent 不同，但两条 durable parent 链都必须在子节点注册前存在且无环。
5. Portal 是现有 Logical subtree 的投影，不是结构移动。Portal root 保留 Component/Logical parent；在挂载期间成为独立 Layout root，并把 Paint parent 重定向到显式 Overlay Anchor。其后代继续在各自投影中以 Portal root 为父，不重新声明所有权。
6. Overlay Anchor 由 Host/Surface 显式注册，以 `(Session generation, SurfaceId, AnchorId)` 为作用域，并分别给出 Paint 与可选 Semantic parent。跨 Session、Generation、Surface、未知 Anchor、重复挂载和 Paint/Semantic cycle 均失败关闭，使用 `WTUE-TREE-001..004` 诊断。Anchor 的几何只可作为后续 Portal Layout/placement 的只读输入；它不成为 Logical ancestor，也不把源树 Layout/Clip/Transform 隐式继承给 Portal。
7. Overlay 顺序由显式 `OverlayOrder` 决定，同序以 Instance Handle slot 稳定决胜；不得依赖注册、异步完成、TMap 遍历或帧内回调顺序。M4 可以在该顺序下增加 Transform/Clip/Layer 实现，但不能另建与此合同冲突的 z-order 事实源。
8. 非 Modal Portal 的 Semantic parent 继续来自 Logical projection。Modal Portal 只有在 Anchor 明确允许 Modal 且提供 Semantic parent 时才能挂载；其 Semantic root 投影到该 parent。最高 `OverlayOrder` 的 Modal 是当前 Semantic/focus scope，背景与被覆盖的 Modal 对焦点和激活呈 inert，但 Logical ownership 不变。
9. 打开 Portal 前捕获 Focus Restore token：Session、Surface、Portal root，以及 origin→Logical ancestors 的同代 Semantic 候选链。关闭后按该链选择首个仍存活且可聚焦的节点；当前 Modal scope、错误 Session/Surface、旧 Generation、Portal 内 origin 或空候选均失败关闭为无恢复目标，并记录 `WTUE-TREE-005`。本路线不使用 DOM id、Source path 或未来 Stable Semantic Key 跨重导入猜测焦点。
10. Event capture/target/bubble、Cascade/继承、状态与属性所有权永远沿 Logical Tree；Layout dirty 沿 Layout Tree；Display/Clip/Hit/Compositing 沿 Paint Tree；导航、语义边界和 Modal inert 沿 Semantic Tree。任何后续实现不得把 Paint parent 复用为事件或语义 parent。
11. Projection Policy 只裁决父链、挂载、排序、Modal scope 与恢复候选。结构提交仍服从 Session-owned 更新事务与遍历保护；句柄仍服从 ADR-0002 的 Owner/Generation/Slot；属性仍服从 ADR-0006；资源、Transform、Clip、Material、Animation 与实际 Portal lowering 分别由后续路线实现和测量。

## 被否决的替代方案

- **挂载时直接重挂 Runtime Parent/Children**：会让事件、继承、组件状态和 Binding/Behavior scope 随视觉位置漂移，并破坏现有 Generation-safe 事件路径。
- **复制 Portal subtree**：产生双份 Runtime State、句柄、资源与焦点身份，无法定义哪一份接收 Mutation/异步结果。
- **只在 Paint 中临时追加 Overlay 命令**：没有独立 Layout、Semantic scope、Hit Test、cycle、Anchor 或 Focus Restore 合同，后续 M4/M5 仍会各自发明边界。
- **允许跨 Session/Surface Portal**：LocalPlayer、Slate User、World/Surface 生命周期和 Feedback scope 会混淆；跨 Surface 必须由显式 Host/Native Component 边界完成。
- **以 Paint/z-order 作为 Tab/语义顺序**：视觉叠放不等于语义拥有关系；Modal 需要显式 scope，非 Modal Portal 继续服从 Logical projection。
- **通过 id/结构路径自动找回焦点**：当前身份只保证同一 Runtime UI Instance generation；跨重导入匹配属于下一条 Stable Semantic Key 路线，不能在本路线隐式实现。

## 结果与路线约束

- M3.6 实现 `FWebToUETreeProjectionPolicy` 与敌意专项，验证 wrapper flatten、Portal 不改写 Component/Logical owner、独立 Layout root、显式 Paint/Semantic Anchor、cycle/cross-Surface 拒绝、确定性 Overlay、Modal inert 与同代 Focus Restore。
- 当前 `SWebToUEView` 仍使用现有单树 Yoga/Display List/Semantic 路径；UI Source 无 Portal/Overlay/Anchor 声明，Compiler 不生成相关 IR，Runtime 不挂载真实 Portal。上述能力在实现、视觉、输入、性能与 Packaged 门完成前继续标记为未支持。
- M3 下一路线的 Stable Semantic Key 只能用于跨结构修订匹配，不能替代 Instance Handle 的即时生命周期校验或放宽本 ADR 的 Session/Surface 边界。
- M4 必须在同一 Paint/Compositing projection 上实现 Transform、Clip Chain、Layer 与 inverse Hit Test，并单独验证真实视觉/性能；M5 动态 insert/remove/reorder 必须事务化更新五树映射和 Focus Restore。
- Native Component 若投影内部 Semantic nodes，必须通过显式组件/宿主节点接入 Semantic Tree；不得借其 Slate Widget 层级绕过 Logical ownership 或 Modal scope。
