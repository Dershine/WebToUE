# ADR-0002：Template 与 Instance 节点身份

- 状态：Accepted
- 日期：2026-08-13
- 范围：WebToUE Core 与 Runtime 的节点身份、交互状态和 View-owned Cache；不改变 Compiled UI IR 资产 schema

## 背景

当前每个 Runtime UI Instance 都从同一 WTUE Document 水合出独立节点树。运行时状态使用连续数组，但节点以 `RuntimeDataIndex` 或 raw pointer 间接充当身份；Hover/Pressed/Focused 状态以及 Text、Brush、Paint Order Cache 也长期保存 raw pointer。内存地址不能证明节点属于哪个 View 或哪次水合，旧地址在重导入或重建后还可能被复用。这会阻碍后续共享不可变模板、细粒度 Dirty 和跨阶段 Cache 使用同一身份合同。

身份基础建立后，固定 Hydration Corpus 证明每个 View 仍复制全部已水合规则、Selector Index 和节点包装。M2.3 需要在不修改资产 schema、不引入 per-node UObject/UWidget 的前提下裁决共享边界，并为结构修订、重导入、View 销毁以及后续 Dirty/Yoga/Display List 冻结同一身份合同。

## 决策

1. `FWebToUETemplateNodeId` 表示节点在一个不可变 Compiled UI IR 修订中的稠密位置。当前水合按 Compiled Node 数组索引生成，因此同一模板修订的同一节点在多个 View 中取得相同 ID；动态运行时节点没有 Template Node ID。
2. Template Node ID 的稳定域是单个 Compiled UI IR 修订。结构重编译可以重新分配索引；需要跨修订引用时必须同时引入模板修订身份，不能把当前 ID 当作永久语义键。
3. `FWebToUEInstanceHandle` 由 Runtime UI Instance Owner ID、非零 Generation 和稠密 Slot 组成。所有静态及动态运行时节点都注册到 View-owned Slot Table；解析时必须同时匹配 Owner、Generation 和 Slot。
4. Runtime UI Instance 每次 Reset/Hydrate 都推进 Generation。跨 View、旧水合代次或无效 Slot 的 Handle 解析为 null；交互状态不再保存长期 raw pointer。
5. Text Layout、Brush 与 Paint Order Cache 使用 Instance Handle 作为键或值。树的父子链接和一次调用内的遍历仍可使用非拥有 raw pointer，但 raw pointer 不再承担跨帧、跨重建的身份合同。
6. 独立文本测量辅助路径没有 Runtime UI Instance 节点身份，必须使用显式临时 Layout Cache；生产布局入口继续要求有效 Instance Handle。
7. `UWebToUEDocument` 继续拥有并共享序列化 Compiled payload；每个文档修订惰性构建一个不可变 Runtime Style Template，包含已水合 Style Rules 与 Selector Index。多个 View 共享同一模板；`CommitCompiledDocument` 原子丢弃缓存，已存在的 View 可安全持有旧修订直到收到文档变化通知并重水合。
8. 每 View 继续拥有运行时节点包装、Parent/Children、Instance Handle、Selector Identity、NodeState、Computed Style/Layout 和 Presentation Cache。Binding 会插入动态文本子节点，且这些字段依赖 View/Generation；当前不把它们塞回共享 Compiled 数据，也不为共享而增加第二套 overlay API。
9. Runtime State 与 Render Data 由 Instance Handle Slot 索引；Yoga Measure Context 保存 Document + Instance Handle 并在回调时解析；Text、Brush 与 Paint Order 使用 Handle。未来 Dirty Graph 和 Display List 必须以 Instance Handle 标识实例目标，M3 Stable Key 负责跨结构更新匹配后再生成/复用实例 Handle，不能直接替代生命周期校验。
10. 结构提交发布新 Style Template；`NotifyDocumentChanged` 使 UWidget-hosted View 重水合并推进 Generation。跨 View、旧修订、旧 Generation、已销毁 View 或无效 Slot 的 Handle 均解析为 null。
11. 本裁决不增加 UObject/UWidget per-node，不修改 Compiled UI IR payload、自定义版本、模块依赖或旧资产行为。

## 被否决的替代方案

- **继续使用 raw pointer**：地址无法验证 View、代次或 Slot，且 allocator 复用会让过期身份表面有效。
- **仅使用数组索引**：不同 View 和重水合后的相同 Slot 会冲突，不能阻止旧引用命中新节点。
- **为每个节点持久化 GUID**：立即扩大资产 payload、版本迁移和 Cook 边界，而当前需求只需同一模板修订内的稠密身份。
- **使用 Selector、DOM `id` 或结构路径作为身份**：这些值可缺失、重复或随样式/结构编辑变化，不是可靠的节点生命周期键。
- **每节点 UObject/UWidget/Slate Widget**：违反单 Slate Leaf 和轻量连续 Runtime Data 的既有边界，并显著提高内存及生命周期成本。
- **共享整棵已水合节点树**：Parent/Children、动态 Binding 子节点、Instance Handle 和 Selector Identity 当前与 View 生命周期耦合；直接共享会要求引入实例 overlay、把所有节点字段改为间接访问，并扩大 M2.4/M2.5 的失效改造面。现有数据支持先共享规则/Selector Metadata，节点包装保持可逆。
- **每 View 继续复制规则与 Selector Index**：实现简单，但 500/2,000/10,000 Corpus 会为第二 View 重复 `84,496/205,456/205,456 B`，且每次 Hydrate 重复规则转换和索引构建；收益明确，故否决。
- **直接从序列化 Compiled Rules 匹配**：会让 Runtime 热路径理解资产兼容 payload，并重新引入旧 Name/Value 解析分支；不符合类型化 Runtime 边界。

## 结果与约束

- 同一 Compiled Template 节点可在多个 View 中用 Template Node ID 对齐，而每个 View 的 Instance Handle 保持隔离。
- Compiled payload、已水合 Rules 和 Selector Index 按文档修订共享；每 View 节点包装与状态仍保持隔离。
- 500/2,000/10,000 的共享 Style Template 为 `84,496/205,456/205,456 B`；每 View known-owned 从稳定身份基线 `916,030/3,308,234/14,373,224 B` 降至 `691,958/2,761,322/13,826,312 B`。第一个 View 加共享模板后的可归因容量仍低于旧基线，第二个 View 则完整省去共享模板容量。
- Hydrate P95 从稳定身份基线 `2.256501/16.173001/78.366399 ms` 变为 `2.266999/15.088700/76.560199 ms`。这些 Editor Development 样本证明改造未形成明显回归，但不宣称 Packaged Runtime 或产品级性能改善。
- 动态节点必须在加入运行时树时调用统一注册入口；未注册节点不能进入生产 Cache。
- 未来若需要跨模板修订保存 Template Node ID，必须新建包含修订身份、迁移和失败诊断的 ADR/资产版本闭环。

## 迁移成本与回退

- 现有资产无需迁移；Runtime Style Template 是非序列化派生缓存，失败时 Hydration 返回失败并保留原有诊断边界。
- 现有 Runtime 消费者从直接访问每 View `Rules/SelectorIndex` 改为只读访问器；未来新增规则元数据必须进入同一共享模板构建步骤。
- 保留每 View 节点包装避免本轮引入大范围字段访问器/overlay 重写。若 M2.7/M2.9 的真实 Packaged 每 View 或第二 View 内存证明仍不可接受，可在不改变 Template/Instance 身份合同的前提下把不可变节点字段移入共享 Template Node Data。
- 回退共享规则缓存只需恢复每 View 规则水合与索引初始化，不影响资产 schema 或 Instance Handle；因此当前选择保持可逆。

## 验收

- 两个 View 对同一 Compiled Node 获得相同 Template Node ID、不同 Instance Handle，且不能交叉解析。
- 动态节点获得有效 Instance Handle，但不冒充 Compiled Template 节点。
- 重水合可以复用 Slot，但必须推进 Generation；旧静态及动态 Handle 均不能解析为新节点。
- 两个 View 共享同一 Runtime Style Template；结构提交产生新模板，旧 View 在刷新前可安全持有旧模板。
- `NotifyDocumentChanged` 自动刷新 UWidget-hosted View，结构修订、重导入和 View 销毁后的旧 Handle 均不能解析。
- Hover/Pressed/Focused、Yoga Measure、Text、Brush、Paint Order 和 Runtime State/Render Data 不再以 raw pointer 作为跨阶段身份。
- Text Wrapping、Rich Text、Flex/Measure、Paint Order、Runtime Instance/Presentation Isolation、Hydration Corpus 和完整 WebToUE Automation 保持通过。
