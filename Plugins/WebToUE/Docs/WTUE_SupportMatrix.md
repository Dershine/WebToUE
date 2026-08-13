# WebToUE 当前支持矩阵

> 文档职责：记录 WTUE Web Subset、绑定、输入、资源、诊断与资产行为的精确当前边界。
>
> 当前基线：2026-08-13，M2.5 closure。
>
> 工程状态与路线入口：[WTUE_TechnicalSummary.md](WTUE_TechnicalSummary.md)

本文只描述当前承诺，不记录实现历史。支持边界改变时，必须同时更新对应测试、本矩阵和 Technical Summary 的高层能力判断。

## 1. HTML 与 Authoring

标签：

- 文档：`html`、`head`、`body`
- 样式：`style`、`link`
- UI：`div`、`span`、`p`、`img`、`button`
- 行内语义：`strong` / `b`、`em` / `i`、`u`、`br`

解析能力：

- 双引号、单引号、无引号和布尔属性。
- HTML 注释、doctype 跳过和常见/数值实体解码。
- `<style>`、外链 CSS 和元素 `style`。
- 未知标签保留为通用 Flex 容器并产生警告。
- 文本首尾空白裁剪；尚非完整浏览器空白折叠语义。

本地化声明：

- `data-ue-loc-key`
- `data-ue-loc-namespace`
- `data-ue-string-table` + `data-ue-string-key`
- `data-ue-rich-text="true"`

关键产品文案应使用显式 key；无 `id` 节点结构重排可能改变自动作者路径。

## 2. CSS

选择器：类型、Class、ID、复合、后代、直接子代、逗号组，以及 `:hover`、`:active`、`:focus`、`:disabled`。

布局与可见性：

- `display`、`position`、`visibility`、`overflow`
- `width/height`、`min-*`、`max-*`
- `left/top/right/bottom`
- `margin`、`padding` 及四方向属性
- `gap`、`row-gap`、`column-gap`

Flex：

- `flex`、`flex-direction`、`flex-wrap`
- `flex-grow`、`flex-shrink`、`flex-basis`
- `justify-content`、`align-items`、`align-self`

绘制与文本：

- `color`、纯色 `background/background-color`
- `border`、`border-color/width/style/radius`
- `opacity`、`z-index`
- `font-family/size/weight`、`text-align`、`white-space`
- `object-fit: fill/contain/cover`

值：`px`、百分比、零、部分 `auto`；Hex 颜色和少量命名色。尚无 `rgb()`、变量、`calc()`、渐变和完整颜色集合。

声明表示与顺序：当前 52 个受支持属性在编译期映射为稳定 Property ID，并把 keyword、number、integer、length/edges、color、string、flex 和 border 值解析为类型化 payload；同一份 Property Metadata 统一提供稳定名称、继承性及 Style/Measure/Layout/Paint/HitTest/Resource 影响分类。规则和元素 `style` 的正常 Runtime 热路径不再解析属性名或值字符串。有效声明按源顺序进入 Compiled UI IR；固定 Property winner slots 按 inline origin、specificity、source order 和 declaration order 决定胜者，无效声明产生警告且不参与级联。`margin`/`padding` 四边、`gap` 两轴、`flex` 已提供的 grow/shrink/basis、`background` 纯色及 `border` 已提供的 width/color 会与对应 longhand 竞争同一规范槽位。`border: none` 仍按零宽度 lowering；当前不是完整 CSS 四边 border/style reset 模型。

显式继承：`color`、`font-family`、`font-size`、`font-weight`、`text-align`、`white-space`。

## 3. 绑定、事件、输入与资源

绑定：

| 声明 | 当前行为 |
| --- | --- |
| `data-ue-bind-text="Property"` | 根 UObject 属性转文本 |
| `data-ue-bind-visible="BoolProperty"` | 控制可见性 |
| `data-ue-bind-enabled="BoolProperty"` | 控制可用状态和 `:disabled` |
| FieldNotify | 订阅实际使用的根字段；编译 Root Field→Binding Op→Runtime Instance Handle 直接索引，单字段只读取一次并更新直接依赖节点 |

当前绑定只支持根 UObject 属性；嵌套路径、Converter 和双向绑定仍属 M3。文本绑定以 Instance Handle 保留节点级 Text Layout Cache；Cache Key 覆盖显示文本、RichText 模式、字体、颜色/文本样式、当前 Culture 和换行约束。文本值变化只重算目标文本；Desired Size 相同仅重绘，变化时记录目标 Measure 与文本到根的 Layout 依赖路径。`visible` 只产生 Paint/HitTest 影响；`enabled` 在同一 FieldNotify 刷新内先更新 Disabled Pseudo State，再匹配 `:disabled` 及其编译依赖目标。

事件：`data-ue-on-click="EventName"` 广播 `EventName` 和 `ElementId`。

输入：鼠标移动/点击/滚轮、Tab/Shift+Tab、Enter/Space。尚无触摸、手柄、IME 和可访问性导航。

图片：`src` 使用 Unreal 软对象路径，例如 `/Game/UI/T_Logo.T_Logo`；不支持磁盘图片和 HTTP 下载。仅影响 Paint 的 Pseudo State 变化按 old/new 属性差异只更新受影响目标：保留已加载图片、无关 Brush、Text Cache 与 Paint Order，且不进入 Measure/Yoga；背景变化可重建该目标 Brush。根字段 text/visible/enabled 绑定不会进入资源加载；Resource 影响或完整文档刷新仍可能同步加载，Resource Manifest 与完整 Resource Cache 属于 M2.6。

## 4. 诊断与资产行为

当前诊断覆盖：

- HTML/CSS 文件读取失败。
- 标签名缺失、未匹配闭合标签、未知标签。
- CSS 规则未闭合、声明格式错误。
- 不支持的 at-rule、选择器、属性和值。
- 外链、内联样式的实际文件、行和列。

第一次导入错误不会产生有效运行数据；已有资产重导入失败（包括 UI Source 缺失）保留上次成功运行数据并更新诊断。

WTUE Document 使用自定义版本 GUID，当前版本 `CompiledBindingOps`（5）包含初始 Compiled Document、本地化富文本、有序声明、类型化样式声明和根字段 Binding Op 演进。已加载旧资产会请求源文件重编译；版本 3 声明在无法立刻重编译时可于 Hydration 一次性解析兼容 payload，版本 4/5 writer 不再写入旧 Name/Value 字符串。项目内 MainMenu/HUD 已从保留的 UI Source 重编译为版本 5；全局未加载资产扫描和完整字段级迁移仍属于 M6。

## 5. 明确尚未支持

- 输入框、文本编辑、IME、表单语义。
- 可见滚动条、拖拽/触摸滚动、惯性和虚拟列表。
- 手柄导航、CommonUI 深度集成、无障碍语义。
- 嵌套属性路径、Converter、双向绑定、类型化事件载荷。
- 组件、Props、Slots、条件节点、循环和 Keyed Diff。
- Transition、Keyframes、Transform、阴影、渐变、滤镜和 Mask。
- CSS Grid、Table、Float、CSS Variables、`calc()`、媒体查询。
- 独立样式/布局/事件检查器、性能时间线和跨平台矩阵。
