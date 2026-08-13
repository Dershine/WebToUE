# ADR-0002：Template 与 Instance 节点身份

- 状态：Accepted
- 日期：2026-08-13
- 范围：WebToUE Core 与 Runtime 的节点身份、交互状态和 View-owned Cache；不改变 Compiled UI IR 资产 schema

## 背景

当前每个 Runtime UI Instance 都从同一 WTUE Document 水合出独立节点树。运行时状态使用连续数组，但节点以 `RuntimeDataIndex` 或 raw pointer 间接充当身份；Hover/Pressed/Focused 状态以及 Text、Brush、Paint Order Cache 也长期保存 raw pointer。内存地址不能证明节点属于哪个 View 或哪次水合，旧地址在重导入或重建后还可能被复用。这会阻碍后续共享不可变模板、细粒度 Dirty 和跨阶段 Cache 使用同一身份合同。

M2.3 尚未裁决哪些节点、规则和 Selector Metadata 应共享，也不应为身份切片提前修改资产版本或强制重写 Hydration。当前需要先建立能验证 View 和生命周期、又不依赖 UObject/UWidget per-node 的最小身份基础。

## 决策

1. `FWebToUETemplateNodeId` 表示节点在一个不可变 Compiled UI IR 修订中的稠密位置。当前水合按 Compiled Node 数组索引生成，因此同一模板修订的同一节点在多个 View 中取得相同 ID；动态运行时节点没有 Template Node ID。
2. Template Node ID 的稳定域是单个 Compiled UI IR 修订。结构重编译可以重新分配索引；需要跨修订引用时必须同时引入模板修订身份，不能把当前 ID 当作永久语义键。
3. `FWebToUEInstanceHandle` 由 Runtime UI Instance Owner ID、非零 Generation 和稠密 Slot 组成。所有静态及动态运行时节点都注册到 View-owned Slot Table；解析时必须同时匹配 Owner、Generation 和 Slot。
4. Runtime UI Instance 每次 Reset/Hydrate 都推进 Generation。跨 View、旧水合代次或无效 Slot 的 Handle 解析为 null；交互状态不再保存长期 raw pointer。
5. Text Layout、Brush 与 Paint Order Cache 使用 Instance Handle 作为键或值。树的父子链接和一次调用内的遍历仍可使用非拥有 raw pointer，但 raw pointer 不再承担跨帧、跨重建的身份合同。
6. 独立文本测量辅助路径没有 Runtime UI Instance 节点身份，必须使用显式临时 Layout Cache；生产布局入口继续要求有效 Instance Handle。
7. 本切片不共享节点/规则、不改变 Yoga 所有权、不增加 UObject/UWidget per-node，也不修改 Compiled UI IR payload、自定义版本或旧资产行为。共享边界将在后续 M2.3 数据裁决中单独决定。

## 被否决的替代方案

- **继续使用 raw pointer**：地址无法验证 View、代次或 Slot，且 allocator 复用会让过期身份表面有效。
- **仅使用数组索引**：不同 View 和重水合后的相同 Slot 会冲突，不能阻止旧引用命中新节点。
- **为每个节点持久化 GUID**：立即扩大资产 payload、版本迁移和 Cook 边界，而当前需求只需同一模板修订内的稠密身份。
- **使用 Selector、DOM `id` 或结构路径作为身份**：这些值可缺失、重复或随样式/结构编辑变化，不是可靠的节点生命周期键。
- **每节点 UObject/UWidget/Slate Widget**：违反单 Slate Leaf 和轻量连续 Runtime Data 的既有边界，并显著提高内存及生命周期成本。
- **本轮同时完成共享模板重写**：缺少节点/规则/Selector Metadata 的多 View 收益和迁移成本裁决，会把可独立验收的身份基础扩大为不可逆架构阶段。

## 结果与约束

- 同一 Compiled Template 节点可在多个 View 中用 Template Node ID 对齐，而每个 View 的 Instance Handle 保持隔离。
- Handle 增加每节点身份字段和 View-owned Slot Table 的容量成本；它是后续消除深拷贝与统一 Dirty/Cache 身份的基础，不宣称当前 Hydration 或常驻内存改善。
- 动态节点必须在加入运行时树时调用统一注册入口；未注册节点不能进入生产 Cache。
- 未来若需要跨模板修订保存 Template Node ID，必须新建包含修订身份、迁移和失败诊断的 ADR/资产版本闭环。

## 验收

- 两个 View 对同一 Compiled Node 获得相同 Template Node ID、不同 Instance Handle，且不能交叉解析。
- 动态节点获得有效 Instance Handle，但不冒充 Compiled Template 节点。
- 重水合可以复用 Slot，但必须推进 Generation；旧静态及动态 Handle 均不能解析为新节点。
- Hover/Pressed/Focused 以及 Text、Brush、Paint Order Cache 不再以 raw pointer 作为长期身份。
- Text Wrapping、Rich Text、Paint Order、Runtime Instance/Presentation Isolation 和 Hydration Corpus 保持通过。
