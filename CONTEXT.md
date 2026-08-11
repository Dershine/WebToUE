# WebToUE Domain Language

WebToUE 将前端式 UI 创作体验转换为游戏引擎原生 UI。本文只定义项目中的统一语言，不记录实现方案、版本状态或路线图。

## 创作与编译

**UI Source（UI 源文件）**:
作者维护的声明式界面输入，包括结构、样式、资源引用和引擎桥接声明。
_Avoid_: 网页、Web 页面、运行时页面

**WTUE Web Subset（WTUE Web 子集）**:
WebToUE 明确承诺支持的前端语义集合；它面向游戏 UI，不等同于浏览器标准兼容层。
_Avoid_: 完整 HTML、完整 CSS、浏览器兼容

**Authoring Tree（创作树）**:
UI Source 解析并规范化后、尚未降低为运行时格式的结构化表示。
_Avoid_: Runtime DOM、Slate Tree

**Compiled UI IR（编译 UI IR）**:
由 UI Source 生成、带版本、可验证并可交付给运行时的不可变界面表示。
_Avoid_: Runtime DOM、HTML Cache、Widget Blueprint

**WTUE Document（WTUE 文档）**:
保存 Compiled UI IR、资源依赖和必要元数据的可交付界面单元。
_Avoid_: 网页、DOM Document、HTML 文件

## 运行时

**Runtime UI Instance（运行时 UI 实例）**:
WTUE Document 在一个实际界面视图中的实例，拥有独立的交互、绑定和滚动状态。
_Avoid_: Runtime Document、网页实例

**Runtime State（运行时状态）**:
只属于 Runtime UI Instance 的可变状态，例如焦点、伪状态、滚动位置和绑定值。
_Avoid_: Compiled Node、资产状态

**Data Context（数据上下文）**:
向 Runtime UI Instance 提供可观察游戏状态与命令的宿主对象。
_Avoid_: JavaScript Model、全局变量

**UI Event（UI 事件）**:
由界面声明触发、交给游戏逻辑处理的语义化交互消息。
_Avoid_: DOM Event、JavaScript Callback

## 工程边界

**Native Runtime（原生运行时）**:
不依赖通用浏览器内核解释页面，而由宿主引擎完成状态、布局、输入和绘制的运行时。
_Avoid_: 内嵌浏览器、WebView Runtime

**Editor Automation Surface（编辑器自动化面）**:
面向工具、测试和智能代理公开的受控编辑器能力集合，不属于游戏运行时产品接口。
_Avoid_: Runtime API、远程控制后门
