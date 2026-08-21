# ADR-0006：UI 属性所有权与仲裁

- 状态：Accepted
- 日期：2026-08-20
- 范围：Source、CSS/Pseudo、Binding、Behavior、Animation 与 Material Parameter 对同一 UI 属性地址的所有权、合成和诊断；本 ADR 本身不证明各产品消费者已实现，当前 MID 对象生命周期由 ADR-0011 负责

## 背景

M2 已建立类型化 CSS Property、固定 Cascade slot、根字段 Binding、Runtime State 与 Display List；M3.2～M3.4 又建立事务、事件身份、Clock 和异步取消。后续 Behavior、Animation 和 UE Material 若都能直接修改同一节点值，却没有预先冻结所有权，结果会退化为取决于回调顺序的“最后写入者获胜”：FieldNotify、Pseudo State、动画结束和异步结果会互相覆盖，重导入或不同帧序还可能改变最终画面。

当前 Runtime 已经隐含两种不同语义：文本 Binding 覆盖 Source text，而 CSS `visibility` 与 Binding visibility 是限制性交集。把所有属性强行塞进一张数值优先级表会破坏这些现有语义，也无法安全表达瞬时 Animation overlay 或有类型的 Material Parameter。

## 决策

1. 所有写入先解析为规范属性地址，而不是字符串属性名。地址空间包括 `node.text`、`node.visibility`、`node.enabled`、规范 CSS longhand/computed-style slot、`style.transform`，以及带 Parameter Name 和 Scalar/Vector/Texture 类型的 Material Parameter。CSS shorthand 必须先降低到既有 canonical slots，不能拥有独立 Runtime 地址。
2. Source 是不可变基线。CSS 与 Pseudo selector 共同属于一个 **CSS Cascade baseline**；Pseudo 只改变规则是否参与现有 specificity/source/declaration-order 级联，不因“是 Pseudo”自动高于普通规则或 inline style。
3. Binding 与 Behavior 是 **durable owner**。一个属性地址最多只能由其中一个域持久拥有；二者同时 claim 是编译/验证错误 `WTUE-OWN-003`，不得按注册、事件、FieldNotify 或事务到达顺序选胜者。同一 durable owner 内的多次值提交仍按 M3 更新事务和该 owner 自身的静态控制流排序。
4. Animation 是 **active-only transient overlay**。分层属性的有效顺序是 `Animation(active) > durable owner > CSS/Source baseline`。Track 结束、取消或失活时必须显露当时最新的 underlying value，不能写回或恢复动画启动时缓存的旧值。M4 对同一地址只允许一个活动 lease；新请求必须显式 Retarget、Reverse、Replace、Cancel 或 Reject，不能静默 last-writer。
5. `node.visibility` 与 `node.enabled` 使用 **restrictive gate**，不是覆盖层：Source/宿主约束、CSS/Pseudo gate 和 durable gate 必须全部允许，节点才可见/可用。任何 Binding、Behavior 或动画都不能重新打开被 Source、CSS、Session 或 Host 禁止的节点。Animation 不拥有这两个 gate。
6. `node.text` 使用 layered override：Binding 或 Behavior 可以覆盖 Source/localized text；二者不能同时持有。Animation 不能写文本。释放 durable owner 后重新显露当前 Source/localized baseline。
7. 只有受控表现属性可进入 Animation overlay。当前 M3 合同允许 Color、Background Color、Border Color、Opacity、Visual Transform，以及显式声明的 Scalar/Vector Material Parameter；Width/Layout、Visibility、Enabled、Text 和 Texture Parameter 均拒绝。M4 若扩展集合，必须同时更新类型元数据、工作量门和本 ADR 的实现证据。
8. Material Parameter 是独立的类型化地址，不能由 CSS 名称、任意字符串属性写入或反射猜测。Material 默认值是 Source baseline；Binding/Behavior 可成为唯一 durable owner；Scalar/Vector 可有 Animation overlay，Texture 不可动画。Material/MID 实例、GC、资源驻留和参数存在性由 M4 Resource/Ownership 合同与 [ADR-0011](ADR-0011-View-Owned-MID-And-Material-Parameter-State.md) 负责。
9. 冲突诊断必须使用稳定 code、规范属性地址和排序后的 source location，因而不受 AST、Selector、Binding 或模块遍历顺序影响。`WTUE-OWN-001` 表示无效/未类型化地址，`WTUE-OWN-002` 表示 writer 不允许，`WTUE-OWN-003` 表示 Binding/Behavior durable owner 冲突。
10. 所有 durable 值变化和 Animation lease 变化必须进入 Session-owned 更新事务并服从 Instance Handle/Generation；Policy 只裁决所有权域，不绕过事务、生命周期、Dirty 传播、资源加载或缓存所有权。Compiled IR、Runtime State、Render Data 与 Track/MID cache 继续分离。

## 被否决的替代方案

- **最后写入者获胜**：实现最少，但结果依赖回调与帧序，动画结束会恢复陈旧值，无法确定性重放。
- **固定 `Animation > Behavior > Binding > Pseudo > CSS` 数值表**：看似明确，却让 Binding 与 Behavior 的双重事实源长期并存，并错误地把 Pseudo 从 CSS Cascade 中拆出。
- **Binding 永远高于 Behavior（或相反）**：掩盖作者错误，异步或组件复用时很难解释值为何不可写；应在编译期要求唯一 durable owner。
- **Animation 把终值写回 durable state**：混合 Track 与 Runtime State 生命周期，Retarget/Cancel/重导入时会覆盖更新后的 Binding/Behavior 值。
- **把 Material Parameter 当作任意 `FName→variant` Map**：会形成字符串反射、类型漂移、缺参静默失败和不可审计的资源/MID 生命周期。

## 结果与路线约束

- M3.5 只冻结并实现 ownership policy、稳定诊断和现有 CSS/Pseudo/Binding 行为的一致性专项；不增加 Behavior Source、Animation Track、Material/MID 或新的 UI Source 语法。
- M4 的 Transition/Animation 与 Material 实现必须消费同一 canonical address 和 policy，显式实现 lease/retarget/release，不得建立第二套优先级。
- M5 的 Behavior Compiler 必须与 Binding claim 一起运行静态 ownership validation；发生 `WTUE-OWN-003` 时不生成可运行 Behavior IR。
- M6 的 Source Map/Inspector 必须能显示 baseline、durable owner、active overlay、restrictive gate、来源和冲突 code。
- Binding text/visible/enabled 与 M4.3b C++ typed Scalar/Vector Material Parameter submission 已是产品可用 durable owner 消费者；Behavior IR、Animation Track、参数作者语法和 Texture Parameter 仍在各自实现/资产/Packaged 门完成前标记为未支持。
