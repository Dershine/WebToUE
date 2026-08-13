# ADR-0003：Compiled Resource Manifest 与异步稳定句柄

- 状态：Accepted
- 日期：2026-08-13
- 范围：WTUE Document 资源依赖、资产版本 6、Runtime Instance 与 View-owned Presentation Resource Cache

## 背景

M2.5 前，图片 Brush 在 Presentation 重建时按节点 `LoadObject`，字体解析通过 Settings 的软对象执行 `LoadSynchronous`。同一路径可被多个节点重复查找，完整刷新或 Resource 影响可能在状态变化路径同步加载；Compiled Asset 又将纹理和 String Table 分散保存在不同数组，没有字体依赖或统一资源身份。该结构不能证明 Cook 依赖完备，也不能为失败、取消、首帧与内存提供统一工作量。

M2.6 需要在不引入浏览器资源模型、每节点 UObject/UWidget 或通用下载器的前提下，冻结编译资源身份、View 创建阶段解析、热路径消费和生命周期取消边界。

## 决策

1. Compiled UI IR 使用单一 `FWebToUECompiledResource` 数组保存 Texture、Font 和 String Table；条目由 `(Kind, FSoftObjectPath)` 唯一标识并在编译期去重。
2. 清单数组索引是在单个 Compiled Asset 修订内的稳定 Resource Handle。结构重编译可以重新排序；Handle 不可跨资产修订持久化，也不是公开内容 ID。
3. 资产自定义版本升至 `CompiledResourceManifest`（6）。旧资产必须从保留 UI Source 重编译；0.5 不承诺长期字段级迁移。
4. Runtime Hydration 把只读 Manifest 复制到每个 Runtime Instance。每个 Presentation 创建同长度的 `TStrongObjectPtr<UObject>` 槽位，槽位索引与 Manifest Handle 对齐。
5. View 创建或显式 Resource 重建时，先以 `ResolveObject` 消费已驻留对象，再把其余唯一路径交给 `FStreamableManager` 批量异步请求。生产路径不使用 `LoadObject` 或 `LoadSynchronous`。
6. 异步完成委托只持有弱 Slate Widget 引用并触发布局/绘制失效；资源对象在下一次 Layout/Paint 边界写入稳定槽位。Presentation 本身不被回调捕获，避免 View 销毁后的悬挂访问。
7. Reset、文档替换和析构取消未完成的 View-owned 请求。多个 View 不共享请求或强句柄数组，但解析到同一引擎拥有 UObject，因此跨 View 不复制底层资源对象。
8. 图片和字体热路径只按 `(Kind, Path)` 查 Manifest Handle 并读取槽位。类型不匹配或加载失败采用无图片 Brush/默认 Core Font 回退，记录失败，不在热路径同步重试。
9. Telemetry schema 8 增加 Manifest 条目、异步请求、驻留命中、失败、取消和已知自有句柄字节；`resource_load_attempts` 继续作为同步加载守门并在状态变化路径保持 0。
10. String Table 当前进入 Cook/依赖 Manifest，但本地化文本身份仍由 Compiled `FText`/String Table Registry 语义消费；Presentation 不为每个 String Table 建立节点对象。

## 被否决的替代方案

- **继续按节点同步加载**：实现简单，但让状态变化延迟依赖磁盘/IoStore，并重复路径查找，无法建立零同步加载门。
- **每类资源独立数组**：保留旧结构，但字体仍是旁路，Handle、Telemetry、失败与取消逻辑继续分叉。
- **以软路径为热路径 Map Key 且不设稠密槽位**：可以工作，但每次 Paint/Text 都需要 Hash/字符串路径查找，且不利用编译资产已有的稳定顺序。
- **全局 WebToUE 资源对象缓存**：可能减少 View 数组，但会复制 UE Asset Manager/GC 职责并引入全局驱逐、预算和线程安全策略；当前由引擎共享 UObject，View 只拥有轻量强句柄。
- **异步回调直接捕获 Presentation**：完成处理直接，但 View 销毁、文档切换和请求取消会形成生命周期竞态。
- **每节点保存 UObject/Brush 资产引用**：扩大 Runtime 节点 payload，重复相同依赖，并违反共享 Compiled IR 与 View-owned Cache 分离。
- **网络/磁盘动态资源下载器**：超出 PersonalGame-ready 0.5 的 Cooked Unreal 资产边界，并会引入安全、缓存、重试和发布协议。

## 结果与约束

- Compiled Asset 可完整枚举三类当前资源依赖；相同资源跨节点只占一个 Manifest 槽位。
- 多个 View 共享引擎 UObject 身份，同时保持请求取消、强引用和失效生命周期隔离。
- Resource 解析可能异步完成，初始帧必须接受确定性回退；完成失效后再生成图片 Brush/字体布局。
- Manifest Handle 只在资产修订内稳定。未来若需要跨修订资源身份、优先级、分组、流送预算或热替换，必须扩展版本化条目并记录新的 ADR/迁移门。
- `ResourceKnownOwnedBytes` 仅统计 WTUE 已知的句柄数组容量，不等同于资源内容、进程 RSS、LLM 或 VRAM。真实 Packaged 内存仍由 M2.7/M2.9 验证。

## 迁移成本与回退

- MainMenu/HUD 已从保留 HTML/CSS 源重编译到版本 6；旧版本在加载时请求重编译。
- 回退为旧独立数组会丢失字体依赖、稳定 Handle 和统一 Telemetry，并要求再次提升资产版本；因此不作为无版本回退。
- 若后续 Packaged 证据证明每 View 强句柄数组不可接受，可在保持 Manifest Handle 合同的前提下，把已解析对象表移动到文档修订级共享缓存；请求取消和 View 失效仍需保持独立。

## 验收

- `ResourceManifest` 覆盖 Texture/Font/String Table 和重复图片去重。
- `ResourceLifecycle` 覆盖稳定 Handle、跨 View UObject 共享、冷/暖路径、首次新字形、类型失败回退、异步请求取消和已知自有字节。
- `PaintOnlyPseudoResourceSafety` 证明状态变化不进入同步图片加载并保留无关 Brush。
- Runtime 生产源不包含 `LoadObject`/`LoadSynchronous`；完整 Automation 46/46、零警告。
- Win64 Game + Editor Development 和 tracked Cook/Stage/Pak/IoStore 通过；该发布门不替代 Packaged Runtime 视觉、延迟、RSS/LLM/VRAM 或 GT/RT/GPU 验证。
