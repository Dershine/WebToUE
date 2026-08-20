# WebToUE Domain Language

WebToUE 将前端式 UI 创作体验转换为游戏引擎原生 UI。本文只定义项目中的统一语言，不记录实现方案、版本状态或路线图。

## 创作与编译

**UI Source（UI 源文件）**:
作者维护的声明式界面输入，包括结构、样式、资源引用和引擎桥接声明。
_Avoid_: 网页、Web 页面、运行时页面

**Behavior Source（行为源文件）**:
作者使用 WTUE 受限 TypeScript 语法维护的界面局部状态、事件编排和动画调度输入；它不是可任意执行的 JavaScript 程序。
_Avoid_: JavaScript 脚本、游戏逻辑、Runtime JS

**WTUE Web Subset（WTUE Web 子集）**:
WebToUE 明确承诺支持的前端语义集合；它面向游戏 UI，不等同于浏览器标准兼容层。
_Avoid_: 完整 HTML、完整 CSS、浏览器兼容

**Authoring Tree（创作树）**:
UI Source 解析并规范化后、尚未降低为运行时格式的结构化表示。
_Avoid_: Runtime DOM、Slate Tree

**Compiled UI IR（编译 UI IR）**:
由 UI Source 生成、带版本、可验证并可交付给运行时的不可变界面表示。
_Avoid_: Runtime DOM、HTML Cache、Widget Blueprint

**Compiled Behavior IR（编译行为 IR）**:
由 Behavior Source 提前编译、带版本且受预算约束的界面行为表示，由原生运行时按事件执行。
_Avoid_: JavaScript 字节码、脚本 VM、Gameplay 逻辑

**Typed Mutation（类型化变更）**:
Behavior、Binding 或宿主提交给 Runtime UI Instance 的原子界面变更意图，目标和值均经过编译期验证。
_Avoid_: DOM 操作、字符串属性写入、即时树修改

**WTUE Document（WTUE 文档）**:
保存 Compiled UI IR、资源依赖和必要元数据的可交付界面单元。
_Avoid_: 网页、DOM Document、HTML 文件

**Resource Identity（资源身份）**:
一个 WTUE Document 合同修订内用于稳定引用同一逻辑资源的身份；它不等同于作者路径、UObject 身份或单次编译产生的 Manifest Handle。
_Avoid_: Resource Path、Manifest Index、UObject ID

**Resource Provenance（资源来源）**:
记录资源由哪个 UI Source 声明、作者使用何种引用以及该引用解析到哪个密封依赖的可诊断来源关系。
_Avoid_: 资源身份、机器绝对路径、Runtime 下载地址

**Resource Residency（资源驻留需求）**:
资源相对于 WTUE Document 或其中 Route 的可交互、可见或显式消费时机要求；它描述何时必须可用，不代表资源对象的所有权。
_Avoid_: UObject 生命周期、永久常驻、加载优先级数字

## 运行时

**Runtime UI Instance（运行时 UI 实例）**:
WTUE Document 在一个实际界面视图中的实例，拥有独立的交互、绑定和滚动状态。
_Avoid_: Runtime Document、网页实例

**UI Surface（UI 表面）**:
承载 Runtime UI Instance 的目标呈现环境，例如某个 Local Player 的屏幕层或场景中的世界空间表面。
_Avoid_: 网页窗口、全局 Viewport

**UI Session（UI 会话）**:
把 Runtime UI Instance 与一个 UI Surface、Local Player、数据与命令合同、环境和生命周期绑定的宿主范围。
_Avoid_: 全局 UI 状态、Widget Blueprint 实例

**Template Node ID（模板节点 ID）**:
节点在一个不可变 Compiled UI IR 修订中的稳定位置身份；同一模板节点在不同 Runtime UI Instance 中保持一致，但不作为跨重编译的语义身份。
_Avoid_: Node Pointer、DOM Node ID、跨版本语义 ID

**Instance Handle（实例句柄）**:
可验证 Runtime UI Instance 所有者、代次与节点槽位的短期身份；旧代次或其他实例的句柄不能解析为当前节点。
_Avoid_: Runtime Node Pointer、永久 Node ID、UObject Handle

**Runtime State（运行时状态）**:
只属于 Runtime UI Instance 的可变状态，例如焦点、伪状态、滚动位置和绑定值。
_Avoid_: Compiled Node、资产状态

**Display List（显示列表）**:
由一个 Runtime UI Instance 派生、按绘制顺序排列且可局部更新的渲染意图集合；它属于 View 的 Render Data，不写回 Compiled UI IR。
_Avoid_: Slate Widget Tree、浏览器 Display List、资产数据

**Paint Command（绘制命令）**:
Display List 中由 Instance Handle 拥有的最小绘制单元，携带范围、边界、裁剪和合批兼容信息；它不是独立 UObject 或 Slate Widget。
_Avoid_: Draw Call、Widget、Compiled Node

**Dirty Region（脏区域）**:
局部状态或布局变化后需要重新提交绘制的旧/新可见边界并集；它描述失效范围，不承诺底层 GPU 只重绘该像素区域。
_Avoid_: GPU Partial Present、全局刷新、Dirty Node

**Data Context（数据上下文）**:
向 Runtime UI Instance 提供可观察游戏状态与命令的宿主对象。
_Avoid_: JavaScript Model、全局变量

**UI Event（UI 事件）**:
由界面声明触发、交给游戏逻辑处理的语义化交互消息。
_Avoid_: DOM Event、JavaScript Callback

**UI Command（UI 命令）**:
UI Session 向游戏代码发出的类型化意图；游戏代码负责权限、异步结果和实际 Gameplay 副作用。
_Avoid_: 任意 UFUNCTION 调用、字符串回调、客户端权限证明

**UI Feedback Cue（UI 反馈提示）**:
由界面交互或界面状态结果产生的语义化瞬时表现意图；它描述反馈含义，不指定声音资产、播放 API 或 Gameplay 结果。
_Avoid_: PlaySound、Audio Asset Path、UI Command

**UI Feedback Profile（UI 反馈配置）**:
把 UI Feedback Cue 映射到项目表现策略与资源的可版本化配置，允许按主题、玩家、UI Surface 和平台选择声音或其他本地反馈。
_Avoid_: Widget Sound、硬编码 Sound、Web Audio

**UI Feedback Router（UI 反馈路由器）**:
在 UI Session 上下文中接收 UI Feedback Cue，并把它交给项目表现系统处理的边界。
_Avoid_: 全局 Audio Singleton、Gameplay Command、Native Component

**Native Component（原生组件）**:
通过显式注册合同向 UI Source 开放的 UE 原生界面扩展，用于承载 WTUE 核心语义不应自行复制的专用能力。
_Avoid_: 任意 UObject、隐式 UMG 回退、每节点 Widget

## 工程边界

**Native Runtime（原生运行时）**:
不依赖通用浏览器内核解释页面，而由宿主引擎完成状态、布局、输入和绘制的运行时。
_Avoid_: 内嵌浏览器、WebView Runtime

**Editor Automation Surface（编辑器自动化面）**:
面向工具、测试和智能代理公开的受控编辑器能力集合，不属于游戏运行时产品接口。
_Avoid_: Runtime API、远程控制后门
