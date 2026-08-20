# ADR-0008：Stable Semantic Identity 与跨重导入状态合同

- 状态：Accepted
- 日期：2026-08-20
- 范围：Stable Semantic Key、Component Instance Identity、Source provenance 和跨 Compiled UI IR 修订的状态匹配/重置边界；不增加作者语法、Compiled UI IR 字段或现有 Runtime 产品接入

## 背景

ADR-0002 已把 `FWebToUETemplateNodeId` 限定为单个不可变 Compiled UI IR 修订内的位置身份，把 `FWebToUEInstanceHandle` 限定为一个 Runtime UI Instance Owner/Generation/Slot 内的安全访问身份。每次重水合都推进 Generation，因此旧 Handle 必须失效；当前通过 Compiled Node 数组索引生成的 Template Node ID 也不能跨结构修订承担语义身份。

后续 Component、条件节点、Keyed List、Behavior、Portal 和热重载需要回答另一类问题：新修订中的哪个节点是旧修订中同一作者语义目标，以及哪些状态可以在重新生成 Handle 后有意识地转移。若直接按 DOM `id`、数组位置、source line、结构路径或旧 Handle 猜测，会把重复组件、列表重排、源文件格式化和 allocator/slot 复用误判为同一节点；若无条件复制整个 `FWebToUERuntimeNodeState`，则 Pointer capture、Pseudo、Binding output、Layout/Resource cache、Animation 和晚到异步工作会跨代泄漏。

M3.7 先冻结可验证的 C++ Policy 和失败边界，供 M5 的 Component/Keyed Diff/Behavior 与 M6 的 Source Map/状态保留式热重载消费。本路线不提前加入 `key` 作者声明、Compiler lowering、资产版本、真实 View 状态迁移或跨重导入 Focus 产品路径。

## 决策

1. **Stable Semantic Key** 是显式、大小写敏感、非空的作者语义键。它只在一个 Component Instance 内唯一，不是 DOM `id`、Selector、Template Node ID、source span、结构路径或 Runtime Handle；未显式提供 Key 的节点视为 unkeyed，跨修订必须重置。
2. **Component Instance Identity** 由稳定 Route/Document scope、从根到当前实例的显式 keyed component/list path 和非零 contract version 组成。兄弟重排不改变身份；改变 Route、任一 keyed boundary 或 contract version 会产生不同 Component Instance，并使旧实例状态退出。
3. 节点跨修订匹配的唯一地址是 `(Component Instance Identity, Stable Semantic Key)`。同一 Key 可在不同 Component Instance 中重复；同一 Component Instance 内重复 Key 以 `WTUE-ID-002` 失败关闭，不能由遍历顺序选择赢家。
4. **Source provenance** 是逻辑 source unit 与 source span，只用于诊断和未来 Source Map。文件内换行、格式化或节点移动可以改变 provenance 而不改变 Stable Semantic Identity；绝对机器路径、line/column 和 provenance 不能参与匹配或生成隐式 Key。
5. ADR-0002 继续拥有运行时访问安全。跨重导入规划必须声明同一个 Runtime UI Instance Owner、不同且非零的 previous/current Generation；旧 Handle 只在提交前状态快照阶段标识旧节点，新修订提交后只能使用 current-generation Handle。Stable Key 不能解析旧 Handle、绕过 Session/Surface/Generation 检查或复活已销毁实例。
6. 匹配地址仍需 Node Kind 和非零 State Contract Version 兼容。Kind 或 State Contract Version 改变时，以 `WTUE-ID-003` 记录并重置；不得把同名 Key 的不兼容 payload 强制解释为旧状态。
7. 状态转移是双方 revision 的显式白名单交集，当前合同只有 `LocalState`、`ScrollIntent` 和 `FocusIntent`。Policy 只生成确定性的 pre-commit transfer/rebind plan，不拥有具体状态值；M5/M6 消费者必须在事务边界内读取旧快照、应用到 current Handle，并在新 Layout/Semantic Tree 可用后 clamp Scroll、重新验证 Focus。
8. unkeyed、新增、跨 Component Instance 移动和不兼容节点重置；删除节点显式退出。Pointer capture、hover/pressed/focus Pseudo、旧 Handle、Template Node ID、Binding output、Computed Style、Layout/Text/Paint/Resource cache、Animation overlay、Timer/Command token 和异步 completion 永不由本 Policy 自动复制。旧 Generation 的异步工作继续按 M3.4 取消。
9. `FocusIntent` 不是焦点授权。消费方只能在 current Session/Surface/Generation 的 Semantic Tree、Modal scope、Focusable/Visible/Enabled 条件全部通过后解析为新 Handle；失败时保持无恢复目标，不能回退到 DOM `id`、source path 或旧结构位置。
10. 规划 Action 按 current Handle slot、随后 removed previous slot 排序；诊断按 code、source unit/span 和 detail 排序。输入描述符、Owner/Generation/Component/provenance/node kind/state version 域无效时使用 `WTUE-ID-001` 失败关闭；unkeyed 节点请求保留状态时以 `WTUE-ID-004` 告警并重置。
11. M3.7 的 `FWebToUESemanticIdentityPolicy` 是纯 C++ 前置合同。UI Source/Compiler/Compiled UI IR/`FWebToUERuntimeInstance` 尚不生成或消费 Stable Semantic Identity；当前成功重导入仍会推进 Generation 并重建 View 状态。产品支持必须等待 M5/M6 的作者声明、lowering、事务化应用和真实集成专项。
12. 本路线不增加 per-node UObject/UWidget/Slate Widget、默认 Tick、资源加载、资产 schema、Cook 依赖或模块方向；未来序列化 Stable Key/provenance 时必须在对应 IR/schema 版本和 freshness 路线中验收。

## 状态保留矩阵

| 状态类别 | M3.7 规划 | 消费约束 |
| --- | --- | --- |
| Component/Behavior local state | 双方显式 `LocalState` 且 Kind/State Contract 兼容时可匹配 | M5 必须类型化、事务化，并遵守属性唯一 durable owner |
| Scroll | 只保留 `ScrollIntent` | 新 Layout 完成后 clamp；不复制旧 Layout/Display cache |
| Focus | 只保留 `FocusIntent` | current-generation Semantic/Modal/Session/Surface 重新验证后获取新 Handle |
| Hover/Pressed/Capture/Pseudo | 总是重置 | 由当前 Slate User/Pointer 输入重新建立 |
| Binding output/Animation overlay | 总是重置并重新求值 | 分别服从属性所有权 Policy 与 active-only Animation 合同 |
| Timer/Command/async completion | 总是取消旧代次 | 服从 Session/Generation token 与 exact-once terminal 合同 |
| Style/Layout/Text/Paint/Resource cache | 总是派生/重建或由各自明确 cache key 命中 | Stable Key 不能替代 current Instance Handle cache key |

## 被否决的替代方案

- **复用旧 Instance Handle 或 Slot**：破坏 Owner/Generation/Slot 生命周期校验，并可能让晚到事件命中新节点。
- **用 Template Node ID 跨修订匹配**：它只是单个 Compiled UI IR 修订内的稠密位置；插入/删除会重新分配索引。
- **用 DOM `id`、Selector 或结构路径作为 Key**：可以缺失、重复或随样式/包装结构变化，且无法隔离重复 Component Instance。
- **用 source file/line/column 匹配**：格式化和移动会改变 span，同时会把机器路径变成不稳定构建输入。
- **对相同 Key 无条件复制整个 Runtime State**：会泄漏临时输入、Derived Cache、Binding/Animation owner 和旧异步工作。
- **重复 Key 采用第一个/最后一个赢家**：结果依赖遍历顺序并掩盖作者错误，破坏确定性和可诊断性。
- **立即序列化新字段并接入现有 View**：会同时扩大资产版本、Compiler、Runtime Migration、Focus/Scroll 和 Cook/freshness 边界，超过本次“先冻结身份与状态合同”的微观路线。

## 结果与后续约束

- `SemanticIdentityPlan` 证明 source span 变化不改变匹配、Component sibling reorder 保持实例身份、相同 Key 可在不同实例中复用、保留状态取双方白名单交集，新增/unkeyed/Kind/contract change/removed 各有显式 Action。
- `SemanticIdentityFailures` 证明同 Component 重复 Key、跨代 Handle 域错误和无效 Component path 失败关闭，失败计划不暴露部分 transfer action。
- M5 的 Component/Keyed Diff 必须生成显式 Component Instance Identity 与 Stable Semantic Key，并通过 Session-owned 事务把计划应用到 current Handle；不得另建 DOM-id/position fallback。
- M6 的 Source Map、热重载与 Inspector 必须显示 address、previous/current provenance、disposition、preserved state mask 和 `WTUE-ID-*` 因果。
- ADR-0007 的同代 Focus Restore 仍保持原合同；跨重导入 Focus 只有在 M5/M6 接入本 Policy 并重新验证 current Semantic scope 后才成为产品能力。
