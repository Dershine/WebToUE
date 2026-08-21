# ADR-0011：View-Owned MID 与 Material Parameter State 分离

- 状态：Accepted
- 日期：2026-08-21
- 范围：静态 Material 资源、Runtime Material Parameter State、View/Node MID 生命周期、事务后提交与跨 View 隔离；不定义参数作者语法、Animation/Transition、Material `Time` 保证或跨节点 MID 池化

## 背景

ADR-0003 已让每个 View 通过稳定 Resource Handle、异步请求和强对象槽消费共享资源；ADR-0006 又把 Material Parameter 定义为具名、具 Scalar/Vector/Texture 类型的 canonical property address，并要求 Binding/Behavior durable owner 与未来 Animation overlay 服从同一仲裁和 M3 更新事务。M4.3a 随后证明静态 `UMaterialInterface` 可跨节点/View 共享并直接生成 Slate Material Brush，默认不需要 MID。

动态参数引入了另一种生命周期：参数值是可恢复、可仲裁的 Runtime State，而 `UMaterialInstanceDynamic` 是由该状态派生的 Presentation UObject。若直接修改共享 parent、让 Resource cache 拥有 MID，或只把参数值存在 MID 中，节点/View 会互相污染，事务失败可能留下视觉副作用，reset/重导入/GC 后也无法确定性恢复状态。反过来，如果每个 UI 节点默认创建 UObject，则静态节点也承担对象、GC 和内存成本，违反单 Slate Leaf 与无 per-node UObject 的产品边界。

## 决策

1. `/Game`/`/Engine` Material 或 Material Instance Asset 仍是 Resource Contract 拥有的共享静态 parent。Runtime 不修改 parent，也不把节点参数状态写回 Compiled Asset、Resource Manifest 或 Resource cache。
2. 一个参数的 durable value 与 owner 属于 Runtime State，以 `(Instance Handle, canonical typed Material Parameter address)` 寻址。Scalar/Vector 值和 Binding/Behavior owner 在 M3 State Mutation 中提交；MID 不是该状态的事实源。
3. MID 是 Presentation 派生对象，由单个 View 的 `FWebToUERuntimePresentation` 通过 generation-safe Instance Handle slot 强拥有。Node 不拥有、保存或暴露 UObject；同一节点第一次成功提交显式 runtime-owned 参数状态时，才为该 View/Node 创建一个 MID。实现不读取或比较 parent 当前默认值来猜测是否“数值相同”。
4. 一个 View/Node 至多持有一个 MID；该节点全部已提交 Scalar/Vector Parameter State 都重放到同一 MID。后续提交复用该 MID。不同节点即使参数集合相同也不隐式共享或池化，不同 View 永不共享 MID；它们只共享静态 parent。
5. 参数提交必须先验证 Game Thread、current View/Generation/Instance Handle、resident Material resource、精确全局参数名与 Scalar/Vector 类型，以及 ADR-0006 durable ownership。Texture、未知参数、非有限值、字符串/反射入口和错误 owner 失败关闭。
6. State Mutation 成功提交后，才能以 Post-Commit Effect 创建/复用 MID、重放当前节点的全部参数状态、重建局部 Brush 并 patch 受影响 Display subtree。事务拒绝、预算失败或相同 owner/value 的幂等提交不得产生 MID 或视觉 patch。
7. MID slot 使用强 UObject 引用保证 GC 存活；其 parent 继续由 Resource slot 提供。View reset/destroy、Document Generation 替换和未来节点删除必须释放对应 slot；旧 Handle、其他 View Handle、失活 Session 和晚到异步结果不能复活或重建旧代 MID。
8. Runtime State、Render Data/Display List、Resource cache 和 MID slot 保持独立。Presentation 可从 current Runtime State 重建 MID/Brush；任何缓存清理都不能把派生 MID 值反写为 durable state。
9. 静态节点、无参数变化和无 Active Track 保持零额外 MID、默认 Tick 和插值。未来 Animation/Transition 必须向同一 typed address/事务入口提交 active-only overlay，不得直接持有 MID 或逐帧反射调用 UObject。Material `Time` 继续是不受 WTUE Clock 保证的显式 escape hatch。
10. `WTUE-MID-001` 覆盖无效/不匹配 typed value，`WTUE-MID-002` 覆盖 target/resource/residency，`WTUE-MID-003` 覆盖参数存在性或 durable owner，`WTUE-MID-004` 覆盖线程、代次、Session 和事务生命周期。新增失败边界必须保持确定性并进入 Automation 与 Packaged gate。

## 被否决的替代方案

- **直接修改共享 Material/MI parent**：节点和 View 互相污染，静态 Resource 不再可共享，异步加载与重导入结果依赖消费顺序。
- **把 MID 放进 Resource cache并跨节点/View 共享**：ResourceId 只标识静态资源，不能表达节点 durable owner、Generation 或不同 View 的参数状态。
- **只把参数值存进 MID**：把 Runtime State 与 Presentation UObject 混合，事务回滚、GC、reset 和资源重新驻留后无法恢复确定性 underlying value。
- **每个 Material 节点默认创建 MID**：静态节点也产生 UObject/GC/内存成本，破坏 M4.3a 的共享静态快路径。
- **按参数集合自动池化 MID**：可能减少对象数，但会把节点身份、Animation lease、局部失效、参数更新和释放耦合到隐式共享图；在大型 Corpus 证明对象压力前不引入。
- **State Mutation 前立即调用 MID setter**：事务失败或 owner 冲突时会留下未提交画面，使 Runtime State 与 Display List 分叉。
- **允许作者字符串或 UObject 反射直接写参数**：绕过 canonical type、ownership、事务、Generation 和允许列表，无法稳定诊断或审计。

## 结果与后续约束

- M4.3b 已用 `DynamicMaterialParameterLifecycle`、Performance schema 13 和 Development/Shipping `ResourceMaterialParameterSmoke` 证明首次创建、复用、释放、GC、局部 Brush/Display patch、共享 parent/跨 View 隔离与零同步加载；该受控 K=1 证据不外推 PSO、GPU 材质复杂度、大型资源页或产品级内存。
- M4.6 Animation 与 M5 Behavior 只能消费 ADR-0006 的 address/owner 和本 ADR 的 Runtime State→Post-Commit Presentation 边界，不能新增直接 MID 写入通道。
- M4.9 若以真实大型 Corpus 证明 MID 数量或内存成为风险，可提议显式池化/共享策略；在证明身份、lease、GC、跨 View 和局部失效不退化前，必须保持 per View/Node slot。改变此所有权需要 supersede 本 ADR。
- M6 Inspector 应分别显示静态 Resource、durable Parameter State、active overlay 和派生 MID/Brush，不把 UObject 当前值伪装为唯一事实源。
