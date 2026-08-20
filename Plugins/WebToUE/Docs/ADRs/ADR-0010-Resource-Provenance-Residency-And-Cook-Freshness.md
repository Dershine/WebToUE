# ADR-0010：Resource Provenance、Residency 与 Cook Freshness 合同

- 状态：Accepted
- 日期：2026-08-20
- 范围：资源逻辑来源、Document/Route 驻留分组、密封依赖闭包、Cook freshness stamp，以及 UI/Resource/Behavior/Animation/Interop Schema 独立版本边界；不接入现有 Compiler、Compiled Asset、View 加载或 M4 Material 产品路径

## 背景

ADR-0003 已把 Texture、Font 和 String Table 汇总为单个资产修订内的去重 Manifest，并让每个 View 在创建时解析已驻留对象、批量异步请求其余对象。它解决了状态变化热路径同步加载和 View 生命周期取消问题，但当前 View 仍预请求整个 Manifest；Manifest index 也只是修订内句柄，不能表达跨修订资源身份、作者来源、Route 粒度或首帧优先级。

另一方面，Editor 导入会记录 HTML 与外链 CSS 文件，但 last-good 允许失败重导入继续保留旧 IR。现有资产没有密封依赖闭包、Compiler fingerprint 或 Cook freshness stamp，Cook 无法区分“可发布的已验证 IR”和“源、Schema、资源或 Compiler 输入已经变化的旧 IR”。若后续 Behavior、Animation、Resource 和 Interop Schema 共用一个单调资产版本，任一层的小改动还会迫使所有消费者同步升级。

M3.9 先冻结一个纯 C++、无 I/O 的 Policy 和失败边界，供 M4 的 Resource/Material、M5 的 Behavior/Animation 与 M6 的 Compiler/DDC/Cook 集成消费。它不把合同误写成当前产品已经按 Route 流送或已在 Cook 中执行 freshness gate。

## 决策

1. 一个 `FWebToUEResourceContractDescriptor` 对应一个独立 Cook 的 WTUE Document。`DocumentId`、Dependency、Resource、Route 和 Group 使用大小写敏感的规范逻辑 ID；绝对机器路径、反斜杠、`.`/`..` 段和网络 URL 均拒绝，不能进入确定性输入。
2. Resource provenance 显式区分 `UnrealAsset`、`RelativeSource` 与 `Generated`。每个 Resource 同时引用声明它的逻辑 `SourceUnit` 和实际解析到的密封 Resource/Generated dependency；`/Game`/`/Engine` 软对象、相对作者引用和 `generated:` 引用必须与 Origin 形态一致。HTTP Runtime、磁盘绝对路径和未密封解析继续不在产品边界。
3. `ResourceId` 是单个 Document contract revision 内的稳定逻辑资源身份，不是 ADR-0003 的 Manifest index、UObject identity、Source path 或跨资产公共 ID。未来 Compiler 可把多个节点/Route 指向同一 ResourceId；当前 Manifest Handle 合同不变。
4. 全部 UI/Style/Behavior/Schema/Resource/Generated 输入以 `(LogicalId, Kind, BLAKE3-256 content hash)` 构成确定性排序的密封 Dependency closure。Compiler/配置/lock 输入另以 BLAKE3-256 fingerprint 标识；资源 Manifest 的 provenance、scope、group、residency 和 layer versions 形成独立 BLAKE3-256 hash。哈希生产者必须读原始输入字节，Runtime/Shipping 不回读 Source。
5. Residency assignment 由 `(ResourceId, optional RouteId, GroupId, Class)` 组成。空 Route 是 Document fallback；Route assignment 可把 fallback 提升为同等或更积极的等级，不能降级。`Critical` 必须在 Document/Route 变为可交互前满足，`Visible` 在预测或实际可见时请求且允许确定性 fallback，`Lazy` 只由显式消费动作请求且不进入默认激活。
6. 同一 `(RouteId, ResourceId)` 只能有一个 assignment；未知 Resource、重复 identity、无 assignment、非法 group/class 或 Route 降级全部失败关闭。共享 Resource 可保留 Document fallback，并在一个或多个 Route 中提升；Policy 不拥有实际请求、释放、预算、Chunk 或 GC。
7. UI IR、Resource IR 是必需层；Behavior IR、Animation IR 与 Interop Schema 是显式可缺席层。每层独立使用 Major/Minor：缺席必须为 `0.0`，producer Major 必须等于 consumer Major，producer Minor 不得高于 consumer 支持值。资产自定义版本、Resource Contract policy version 与各层版本互不替代。
8. `FWebToUECookFreshnessStamp` 保存 Contract version、DocumentId、Compiler fingerprint、Dependency closure hash、Resource Manifest hash 和全部 layer versions。Cook 只能把“当前期望 stamp 与编译资产内 stamp 完全相同”判为 fresh；任一 Source/Schema/Resource/Compiler/Manifest/version 变化都以 `WTUE-RES-004` 拒绝 stale artifact，不能因 last-good 存在而静默发布。
9. `FWebToUEResourceContractPolicy` 只验证、规范化、生成 stamp、比较 Cook freshness 和检查 Runtime version compatibility。失败快照不暴露部分 dependency/resource/assignment；`WTUE-RES-001..005` 分别覆盖无效 domain/provenance、重复 identity、residency、stale Cook 与版本不兼容。
10. M3.9 不修改 `UWebToUEDocument`、资产版本 7、Editor import、现有全 Manifest View 请求、Cook 规则、模块方向或加载热路径。M4/M6 产品接入必须序列化 stamp、由 Compiler 生成真实 dependency closure，并在 Cook 开始前用当前输入重建 expected stamp；在此之前 stale-source Cook 仍是明确未支持的产品门。

## 被否决的替代方案

- **继续只依赖 `UAssetImportData` 时间戳/文件列表**：无法覆盖 Schema、Compiler options、生成输入或内容不变时间戳变化，也不能形成跨机器确定性 Cook key。
- **用绝对源路径或 source span 作为资源身份**：机器、格式化和文件移动会改变结果，并违反 ADR-0008 的 diagnostic-only provenance 边界。
- **把 Manifest index 当跨修订 ResourceId**：结构重编译可重排数组，旧 index 不能安全寻址新修订。
- **每个 Route 复制完整 Resource descriptor**：会让共享资源 provenance/hash 漂移；独立 identity + assignment 能明确共享和优先级覆盖。
- **允许 Route 降级 Document Critical**：同一资源的激活要求会依赖进入路径，导致首帧和交互门不可预测。
- **单一全局资产版本覆盖 UI/Resource/Behavior/Animation/Schema**：无关层被迫联动升级，无法表达 optional layer 或 consumer capability。
- **继续使用平台 SHA-256 convenience API**：UE 5.8 Windows 的 `FPlatformMisc::GetSHA256Signature` 无实现并断言；Core `FBlake3` 提供项目已链接、跨平台的 256-bit 内容哈希实现。
- **M3 直接接入 Compiler/资产/Cook 与真实流送**：会同时改变序列化、M4 Resource 产品能力和 M6 工具链，超过本次先冻结合同的微观路线。

## 结果与后续约束

- `ResourceContractCanonicalization` 证明依赖/assignment 输入顺序不影响 closure/manifest hash，Document Lazy fallback 可被 Route Critical 提升，精确 stamp 通过 Cook，源内容变化使旧 artifact 失败，较新 consumer Minor 可消费较旧 producer。
- `ResourceContractFailures` 证明重复 dependency/assignment、网络 provenance、Route 降级、非规范 optional layer 和 Resource IR Major 不兼容失败关闭，失败快照为空。
- M4 必须让真实 Texture/Material/Brush/Feedback resource 生成 ResourceId/provenance/assignment，并用实际 workload 验收 Critical/Visible/Lazy 请求、释放、同步加载、首帧、内存和 Chunk；本 ADR 不提供这些结果。
- M6 必须密封 UI/Behavior/Schema/资源与 Compiler/lock inputs、序列化 freshness stamp，并让 Cook/CI 在 last-good stale 时失败；时间戳或 `.d.ts` 回读不能成为事实源。
- 现有 ADR-0003 继续拥有修订内 Manifest Handle、View-owned request/strong handle 与异步取消；本 ADR 只定义其未来编译输入、scope、优先级和 freshness 上层合同。
