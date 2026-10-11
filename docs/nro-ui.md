# NRO UI 设计依据

普通页面默认使用 Borealis/OpenGL；libnx framebuffer 保留为旧 UI 对照构建。
菜单、连接、BLE、体感、触屏、参数、关于及日志已实现；console 只用于启动和运行错误。
强制规则见 [NRO AGENTS](../nro/AGENTS.md)，用户操作见 [README](../README.md)。
本文只保留设计理由、实测依据与平台限制，不重复组件/参数清单。
2026-10-08 的库迁移调查以原生观感与交互为目标，选定 Borealis 的 dragonflylee/switchfin 分支；
2026-10-10 完成首轮接入，2026-10-11 完成剩余功能页移植，实机验证待完成。
独立 deko3d 呈现后端路线已放弃。

## 实机反馈修正（2026-10-11）

底栏方框定位到 Hint 的图标 Label 使用默认正文中文字体，而自定义加载器没有把
NintendoExt 接入其回退链。图标现在显式绑定已加载的 Nintendo 字体，正文也补齐该回退。
底栏统一隐藏时钟并停止逐帧更新时钟文本；项目特定 vendor 补丁见 UPSTREAM.md。

关于页保留两段说明文本，版本、构建、来源及偏好改为只读框架列表行，保留滚动和
语言/主题快捷键。NRO 的 BLE PoC 菜单项、控制台实现及其语言键已移除；正式 BLE 页的
准备流程和 sysmodule PoC 接口仍需保留。启动/错误 console 继续可用。

主机预览新增关于页并检查实际 Hint 的字体句柄与时钟隐藏状态；普通主机字体不包含
Nintendo 字形，图标的最终显示仍需使用本轮新产物实机复测。
两种 UI 已编译打包，交付目录恢复为 Borealis；`tests/ui`、`lang`、`canvas` 及
72 组两语言、两主题、两分辨率的主机布局检查通过。

## 功能页移植（2026-10-11）

Socket、BLE、体感、触屏和两类日志子页留在同一个 Borealis 窗口内切换；
菜单选中位置保留。移植当时保留的 PoC 控制台已按后续反馈移除，启动和运行期错误继续使用英文 console。
两种 UI 调用同一个 C 页面控制器，复用原有状态结构、配置、波形核心和唯一 `Service*`；
未修改公共 IPC、协议或配置格式。

### 调度、呈现与清理

- 自定义 Platform 每帧只更新一次 PadState、过滤交接持键，再更新活动功能页；
  菜单/关于/参数页调用公共轮询。功能页自行轮询，不额外调用公共轮询。
  业务不经过框架输入回调或 draw，因此输入动画暂停不会暂停采样与波形上传。
- 功能页关闭框架方向键导航，保留 A/X/Y/ZL/ZR 和两路强度操作；日志中只处理
  Y、B 和日志滚动，切换日志重置连发并抑制未释放的按键。普通页 + 无效。
  Y 返回功能页，B 从功能页及其日志直接返回菜单。
- Socket 使用二维码与状态两栏。二维码复用现有编码器，按 drawable 尺寸选择整数
  物理像素模块，关闭抗锯齿并保留四模块静区，只在 Listening/Paired 显示。
  日志控件持有文本副本，按实际字体度量裁剪和加省略号；两类日志保持独立环与 cursor。
- 体感/BLE 为无焦点状态行，触屏在满幅玩法区用 NanoVG 绘制网格和触点。
  保留既有左右分区、轴端点和 720p 坐标映射；页头和底栏高度匹配玩法区边界，
  行布局边距不改变输出坐标。底座模式隐藏触点和网格，按键仍有效。
- 体感离页停止传感器并清空波形，触屏离页清空波形；BLE 返回保留连接，
  准备阶段返回取消准备，X 停止。普通离页、系统退出和异常复用可重复清理入口；
  最终退出在释放图形资源前发送一次 BLE_STOP。console 交接和最终退出才释放窗口、字体与缓存。

### 已执行的验证与边界

两种 UI 的 NRO 编译、链接与打包通过，交付目录最后恢复为默认 Borealis。
`tests/ui`、`motion`、`touch`、`qr`、`lang`、`canvas`、`log` 通过。
新增回归直接执行真实共享控制器，配合模拟 IPC/HID 与真实波形核心，覆盖持键交接、
日志与强度隔离、准备取消、BLE 跨页保留、玩法离页清空和最终清理幂等性。
这不等于实机系统退出或 C++ 异常路径的端到端验证。

可选主机预览使用真实 Borealis/Yoga/NanoVG 页面树与 CoreGraphics 软件渲染；
64 组覆盖两语言、两主题、720p/1080p，以及功能、日志、停止和底座状态。
检查视图边界与日志动作，逐物理像素检查实际二维码绘制与编码矩阵、静区一致。
主机字体与 Switch 系统字体不同，Nintendo 提示图标也不能由普通主机字体验证。
canvas 继续只作为旧绘制回归，预览入口见 [开发文档](development.md#测试与实机边界)。

两种 UI 的日志记录功能页 enter/leave 时间和已用内存，分页面记录最多 240 个
更新间隔样本的 p95/max；框架及 framebuffer 功能页另记录循环帧时间。
框架还记录创建页面到入栈的阶段耗时。这些入口供实机对照使用：更新间隔是
两次业务更新之间的时间，帧时间包含 IPC/绘制与可能的等待，不等于纯 GPU 耗时；
framebuffer 按需重绘，不能将其空闲循环时间直接解释为显示帧率。

尚无本轮 Switch 截图、扫码、多点触屏、Joy-Con 断连/重扫、BLE 跨页/停止、
底座切换、console 往返或性能/内存测量。首轮退出崩溃修复必须优先实机复测，
再核对重复退出、动画期间的玩法更新间隔和两种 UI 的实际成本。

## 首轮移植（2026-10-10）

基线为 `dragonflylee/switchfin` 的 `f347bfd419ae28d466ca7b0957ca73b3c401e957`。
固定源码与必要资源在 `third_party/borealis/`；许可证和本地补丁说明见
[UPSTREAM.md](../third_party/borealis/UPSTREAM.md)。构建使用 Switch portlibs，不进入桌面
FetchContent 路径。OpenGL 是整套 UI 的组成部分，不恢复独立 deko3d 后端路线。

### 接入与兼容

- `UI_BACKEND=borealis` 为默认；`framebuffer` 保留旧 UI 对照构建，均使用原有发布文件名。
  CMake 在 `nro/build/<backend>/` 隔离产物，Make 的构建/打包入口和 NACP 元信息来源保持不变。
- 三个新页面由框架调度；旧功能页仍在 C 层运行。`native.h` 只桥接页面路由、现有状态、
  配置和偏好，不增加 IPC 命令、配置格式或 DG-LAB 连接。异步日志与 BLE 准备流程保持现有实现。
- Borealis 应用只初始化一次。新页面间保留图形窗口；进入旧 UI 时清理动画、UI 回调、
  视图、焦点、延后删除队列和字体句柄，再销毁 NanoVG/GLFW，最后释放共享字体服务。
  返回时重新加载字体；`getDefaultFont` 已移除上游对旧字体句柄的静态缓存。
- 输入读取复用主程序 PadState，不使用上游 SwitchInputManager，不开启普通页的触屏/六轴。
  新页面交接抑制未释放的按键，框架不注册 + 退出。旧页通过已更新的 PadState 接收交接。
- 菜单保留顺序、选中位置、描述和 BLE 活动提示，列表夹紧；关于页上下滚动，左右方向键
  或 L/R 肩键切换语言，Y 切换主题，偏好刷新保留滚动位置。参数使用文字数值，左右单步，
  500ms 后每 200ms 连发，Y 重置；B 保存失败时留在参数页显示失败，避免丢失编辑结果。
- 描述、保存结果和活动会话提示在列表外固定显示；长列表由框架随焦点滚动。
  中文/英文文案仍通过既有语言包和缺键回退；新页面使用 Borealis 明暗主题，旧页面使用原主题。
- 只初始化必要的 RomFS、pl:u 和图形资源；不引入上游启动包装器、网络/亮度/电池服务或音效。
  启动服务/语言包错误保留英文 console；图形和字体异常回到宿主诊断，不静默换成旧 UI。

许可证补充：上游顶层为 Apache-2.0，但通知模块两个文件带 GPL-3.0-or-later 声明。
本次 vendor 省略这两个文件，并禁用所有通知引用；不能将上游整树统称为单一许可证。
未携带上游的 Switch 字体转储，字体从系统共享服务读取；MaterialIcons 保留自己的许可证。

### 已执行的验证

本地使用 devkitA64 r30-1 / GCC 16.1.0、libnx 4.12.0-1、switch-tools 1.13.1-1、
CMake 4.4.3。官方图形包为 GLFW 3.3.4-2、Mesa 20.1.0-5、GLM 0.9.9.7-2、
libdrm_nouveau 1.0.1-2；使用校验过的本地缓存，未修改系统工具链。

- 两种 UI 均完成 NRO 编译、链接及打包；根构建入口完成全部现有组件的发布布局组装。
- 全部现有主机测试及新增 `tests/ui` 通过。新增测试覆盖持键交接、释放再按、参数连发边界、
  换行重置连发、方向改变和长暂停后不补发多步；canvas 的 2215 项检查只证明旧页面。
- 检查 NRO/ASET、NACP 的标题、作者和根 VERSION；Borealis RomFS 含必需图标字体与框架文案。
  发布目录保持 NRO + 原语言包；未提交构建产物。
- 本地构建大小约为 Borealis 8.02 MB、framebuffer 0.405 MB；Borealis RomFS 约 173 KB。
  这些是文件大小，不是运行内存或启动时间。

### 退出崩溃定位与修正（2026-10-10）

两份实机报告（`01791638918_010000000000100d.log`、
`01791638993_010000000000100d.log`）均为 hbloader 下 NRO 的 Data Abort，
结果 `2168-0002`，访问地址为零。模块 Build ID
`fab19a9e83bff2d29f7ee18503d495d922c117e1` 与安装产物对应 ELF 一致。
PC `DGLAB-NX + 0x8c2bc` 映射到 `Application::getNVGContext()`；调用链为
`dglabNativeShutdown → Application::exit → TextureCache::clean`。
宿主先挂起、销毁窗口，随后框架退出事件中的纹理缓存仍无条件获取已释放的显示上下文。
该故障来自本轮 UI 生命周期接入；报告没有指向 sysmodule 或 BLE 线程。

修正为在释放窗口状态时先清理纹理缓存，再销毁上下文；清理同时清空 LRU 列表及两份索引。
退出回调允许重复清理或在窗口已挂起后执行，不访问空窗口，不跨上下文保留纹理 ID。
新增 `tests/ui` 的真实 vendor 缓存回归测试，覆盖空窗口退出、活动纹理释放、重复清理、
上下文重建及 ID 重用。修正后的 NRO 编译与主机测试通过，退出实机复测仍待完成。

最近运行日志记录 `applet=2`（LibraryApplet），框架初始化约 12.26ms，窗口恢复约
132.37ms；这些是对应阶段耗时，不是从点击启动到首帧的总时间，也不证明稳定性验收。

### 待实机验收

本轮没有 Switch 截图、运行帧时间或内存实测，不能将编译成功视为实机可用结论。
日志新增 `ui init/resume/suspend` 的时间、进程已用内存与查询结果，以及每批最多 240 帧的
`p95_ns/max_ns`；帧样本包含 IPC 轮询与呈现等待，可结合现有 SD 日志比较。

| 场景 | 验收内容 |
| --- | --- |
| 三个新页面 | 两语言、两主题、长文本、焦点滚动、菜单位置恢复、参数步进与保存失败 |
| 输入交接 | A 进入不触发下一页，B 返回不退出菜单，普通页 + 无效，触屏仅玩法读取 |
| 显示与资源 | 720p/1080p、底座切换、新旧页与 console 多次往返、字体重开、内存稳定性 |
| 业务兼容 | 二维码扫码、体感/触屏输出、BLE 跨页存活和停止、退出清理、日志连续读取 |
| 失败与模式 | Applet 模式、服务缺失、语言包缺失、RomFS/字体/图形初始化失败的英文诊断 |

实机记录实际启动/切页时间、帧时间和内存，不预填性能改善结论；出现明确可见收益且
上述核心行为仍是实机验收条件。后续功能页移植见上文；CI 已加入依赖检查和旧 UI 构建检查，
本轮未执行远程 CI。

## 呈现与字体选择

普通 Homebrew 没有可直接调用的 HOS 通用控件框架。framebuffer 零额外依赖，
能让 QR 模块保持整数正方形，也使 UI 在电脑上渲染验证。
完整 UI 框架会引入构建、输入与显示生命周期改造；现成库的比较见
[HOS 风格 UI 库迁移评估](#hos-风格-ui-库迁移评估)。Overlay 的环境另行评估。

自绘文本采用 UTF-8、pl 系统共享字体、stb_truetype 与字形缓存；
默认 1-bit 阈值光栅化，匹配点阵观感并复用无 alpha 混合的 canvas。
密笔画可能粘连，改字体/字号需逐字看图，不能仅靠像素越界测试判清晰。

console/字体加载失败的兜底使用 libnx 256 字形字体，不能显示中文；
默认字体符号未公开声明，曾以 nm 与 console 渲染器核对大小和位序，回归见 tests/canvas。
位图兜底在 1.5 倍底座下不随字号缩放，会偏小，不能当原生大字路径。

## 布局与颜色依据

HOS 风格来自用户提供的 1280×720 系统设置截图逐像素测量（2026-09-16 深色、09-18 浅色）。
page/list/theme/text 是尺寸与配色唯一实现来源，不在文档再维护常量表。

- 页头、列表、底栏，无面板；焦点框比行高，裁剪从页头线下开始，不能从首行顶裁。
- 单栏与两栏使用不同内容带，日志用宽带，触屏可声明整块玩法区；不能强行用同一边距。
- 值文字各用独立缓冲；所有行先测量后绘制，滚动随光标；About 等无光标页有独立滚动键。
  About 真实字体实机排不进一屏，不能为省滚动删内容。
- 按键图标按墨迹盒居中，图标与文字两字体分开；line box 居中曾让字母贴边。
- HOS 导航与内容聚焦颜色不同，本项目按用户要求统一成一个主题字段。
- 浅色截图没有对话框或错误状态，因此 dialog/dialog_rule/dialog_button/warn/error
  未量到，暂沿深色值；未来修改必须用含该状态的截图，不凭观感。
- 确认对话框按需求尚不实现；若未来实施，确认右、取消左、默认右焦点是既有选择，
  其它旧量图细节可查 Git 历史。

颜色值已在 theme 中，界面通过 dglabThemeGet 取色。主题取不到系统设置时 Auto=深色；
只在启动/重建读系统设置，挂起时改系统主题需重启 NRO 才跟上，不轮询。

## 本地化与文案

简中/英文 JSON 在根 lang，运行期只读 `SD:/switch/DGLAB-NX/lang/`。
语言自动选择失败或非中文时用英文；有一套可用即可启动，缺 key 回落。
文件名与 language 必须对应；全不可用在 framebuffer 创建前用英文 console 提示并等 + 退出。
未知/重复/缺 key 可记录日志，不能让屏幕空标签。格式与加载细节查 strings/langfiles 及 tests/lang。

专有名词 DG-LAB、Joy-Con、sysmodule、BLE、PoC、Socket、App 和路径保留原文；
统一用“通道强度”“波形值/波形强度”“通道强度上限”“波形强度上限”，不混概念。
菜单名与页面标题共用字符串；DGLAB-NX 只用于菜单标题，版本进 About 行，页头右侧只留服务状态。
语言/主题偏好共存 config/app.cfg，旧文件缺主题时用 auto，不丢原语言。

## 二维码验证

QR 编码独立与 macOS CoreImage 的 CIQRCodeGenerator 逐模块比对，工具和参考矩阵在 tests/qr。
参考返回一模块静区，需要移除；比对样例用纯小写无数字，避免 CoreImage 混合模式分段。
本项目 byte 模式能编码真实地址，只可能比 Apple 多一个版本，不影响扫码。
屏上模块按物理像素取整、静区保持、在可用框内居中；不能用照片观感替代编码验证。

## 底座与显示生命周期

布局始终 720p 逻辑坐标，底座 framebuffer 为 1920×1080（scale=1.5），失败回退 720p。
矩形两边分别换算保持共享边；字体按物理尺寸光栅化、度量返回逻辑单位，QR 模块保持整数像素。
字形缓存容量不足会让整字消失而非裁掉，需与最大字号×scale 一起核对。

操作模式回调只置标志，帧间通过 appDisplaySuspend/Reopen 重建后端与字体，
以 generation 强制所有屏重画；console 共用同一窗口，必须先释放后端再初始化 console。
只重建 framebuffer 不重开字体曾导致新字符全空。
按需重绘要覆盖语言、主题、状态、滚动与 generation；按键必须读本页当帧状态，
不能靠其它页才更新的全局决定启停。

## 日志与 BLE 跨页

Socket 和 BLE 的屏上日志各自一条环与 cursor，不能互相替换标题后共用内容。
原始行保留，显示再按真实像素宽裁并加省略号，不能提前按字符数截断。
NRO 自己的诊断进 net 环，BLE 会话/探针进 PoC 环；net 磁盘镜像可以合写。
BLE 内存日志必须关机前由 NRO 读出。

2026-09-27 选择 BLE 会话跨页存活，菜单提示如何停，玩法连接行显示蓝牙；
退出 NRO 停会话，sysmodule 看门狗兜底。参数页改上限后回蓝牙页会补 BLE_LIMIT，
不为改上限重连。设计/代码已落实，跨页实机仍待验，见 [BLE 验证](ble-poc.md)。

## HOS 风格 UI 库迁移评估

### 当前结论与调查边界

2026-10-08 调查：目标是改善原生观感与交互，允许调整现有页面布局，不要求逐像素保留
现有截图测量结果。这里比较的是 Homebrew 仿 HOS 控件库，不是调用系统原生控件。
选定 **`dragonflylee/borealis` 的 `switchfin` 分支**，固定到
`f347bfd419ae28d466ca7b0957ca73b3c401e957`，先验证 OpenGL 路径。
实际历史比较确认该版本完整包含 `xfangfang/borealis` 的 `wiliwili` HEAD，
并增加与本项目有关的通用修复；详细差异与成本见下文。Aether 保留为次选。

迁移在技术上可行，但属于页面组织、输入调度和显示生命周期的改造，不能只换绘图函数。
选型是结合本项目实现与上游历史、源码作出的判断；调查当时未编译候选库、安装依赖或实机运行。
后续接入与编译结果见本文首轮移植记录，视觉提升、性能和运行内存仍待实测，不以星数推断维护质量。

### 候选与版本依据

下表标有提交的候选已核对调查当日的上游 HEAD，链接固定到完整提交；它们是研究快照，
除已接入的 dragonflylee 外，其它候选未在本项目编译验证。表中 Apache-2.0 指顶层许可证，
实际使用还需检查文件级声明；本次通知模块的处理见首轮移植记录。legacy 仅作旧版 API/许可证比较。
后续验证须固定提交及其子依赖，不能混用不同分支的文档。

| 候选与核对版本 | 已确认的能力与限制 | 本项目判断 |
| --- | --- | --- |
| [Borealis main / 20e2d33](https://github.com/natinusala/borealis/tree/20e2d33b6c4ffce139ce304c503c04f5b94da920) | Apache-2.0；NanoVG、Yoga、自动焦点导航、动画工具、XML、自定义控件与回收列表；README 声明 WIP，Wiki 仅对应旧版；Switch 构建使用 deko3d | 最贴合目标的库家族；上游能力声明不等于各平台实现均已完整或经过本项目验证 |
| [xfangfang/borealis wiliwili / 5f08b28](https://github.com/xfangfang/borealis/tree/5f08b286f3df737f3321d2247a6fe633fcead03c) | Apache-2.0；提供 Switch OpenGL/deko3d 路径；README 列出 wiliwili、Moonlight-Switch 等使用案例 | 比较基线与上游；选定版本已包含此 HEAD，无需另维护该版加补丁的组合 |
| [dragonflylee/borealis switchfin / f347bfd](https://github.com/dragonflylee/borealis/tree/f347bfd419ae28d466ca7b0957ca73b3c401e957) | Apache-2.0；完整包含 xfangfang HEAD，另有 11 个提交；焦点、清理与工具链修复及依赖升级见下文 | 选定迁移基线；OpenGL 接入与编译已完成，仍须实机验收 |
| [Borealis legacy](https://github.com/natinusala/borealis/tree/legacy) | 旧 API/文档体系；[LICENSE](https://github.com/natinusala/borealis/blob/legacy/LICENSE) 为 GPLv3，与现代 Apache-2.0 分支不同 | 不作为迁移起点；不能把现代分支的许可证结论套到旧代码 |
| [Aether master / 29c14b5](https://github.com/tallbl0nde/Aether/tree/29c14b5abf7dba7408ab5941a14ebb2f8396b54c) | MIT；实验性 C++17/SDL2 库，提供 Horizon 列表、菜单、按钮和弹窗；Switch 窗口固定创建为 1280×720 | 次选；现有原生 1080p 目标需要补充显示适配，不能把上采样视作原生 1080p |
| [Plutonium master / c98c7e1](https://github.com/XorTroll/Plutonium/tree/c98c7e1837c76824b27284709c48429d1407e390) | MIT；C++/SDL2 通用 UI，README 列出 SDL2 字体、图片、音频及 Mesa 等依赖 | 可行，但 HOS 风格仍需定制；依赖和资源成本须按实际使用能力核对 |
| [libtesla master / f766e9b](https://github.com/WerWolv/libtesla/tree/f766e9b607a05e9756843cbd62b3bfb98be1646c) | 面向 Tesla Overlay，管理图层、UI、输入和显示/隐藏生命周期 | 留给 Overlay；全屏 NRO 与 Overlay 只能共享业务/IPC，不能假设 UI 环境相同 |

主要来源：Borealis 的 [README](https://github.com/natinusala/borealis/blob/20e2d33b6c4ffce139ce304c503c04f5b94da920/README.md)
与 [Switch 构建定义](https://github.com/natinusala/borealis/blob/20e2d33b6c4ffce139ce304c503c04f5b94da920/library/borealis.mk)；
xfangfang fork 的 [Switch 构建说明](https://github.com/xfangfang/borealis/wiki/NintendoSwitch)
与 [构建实现](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/CMakeLists.txt)；
Aether 的 [Horizon 控件入口](https://github.com/tallbl0nde/Aether/blob/29c14b5abf7dba7408ab5941a14ebb2f8396b54c/include/Aether/Aether.hpp)、
[编译选项](https://github.com/tallbl0nde/Aether/blob/29c14b5abf7dba7408ab5941a14ebb2f8396b54c/Makefile)
与 [窗口/渲染初始化](https://github.com/tallbl0nde/Aether/blob/29c14b5abf7dba7408ab5941a14ebb2f8396b54c/source/Renderer.cpp)。

### 两个 Borealis fork 的差异与选型

2026-10-08 获取两条分支的历史并比较：`merge-base` 是 xfangfang 的 `5f08b28`，
`rev-list --left-right --count` 为 **0 / 11**。dragonflylee 完整包含此次核对的 xfangfang
HEAD，没有丢失其独有提交；这不是两套分叉很远的 UI。
xfangfang HEAD 的提交日期为 2026-04-25，dragonflylee 为 2026-09-22（UTC+8）；
日期仅描述此次快照，不作为维护承诺。[固定版本的完整差异](https://github.com/dragonflylee/borealis/compare/5f08b286f3df737f3321d2247a6fe633fcead03c...f347bfd419ae28d466ca7b0957ca73b3c401e957)。

| 方面 | dragonflylee 相对 xfangfang 的变化 | 对 DGLAB-NX 的影响 |
| --- | --- | --- |
| 焦点导航 | [隐藏 Box 不再向默认焦点搜索提供子控件](https://github.com/dragonflylee/borealis/commit/f347bfd419ae28d466ca7b0957ca73b3c401e957) | 对动态状态、隐藏列表和切页有通用价值；不宣称修复所有隐藏控件场景 |
| 界面清理 | [公开 Application::clear 并清空 focusStack](https://github.com/dragonflylee/borealis/commit/e5136ab978a78839a033ee2e621b1f56c9b92c32) | 减少已删除页面的焦点状态残留；clear 只清理页面，不代替 GPU/窗口挂起与重开 |
| Switch 构建 | [显式把 __SWITCH__ 放进 C/C++ 编译参数](https://github.com/dragonflylee/borealis/commit/0303fe7ff01c318d7268647bbf41917af03f24a6)，替代仅设置同名 CMake 变量 | 平台条件编译更明确；仍须在实际 devkitPro 工具链验证，不能据此判定旧版必然不能构建 |
| 动画与 JSON | [Tweeny 3.2.0 → 4.1.0](https://github.com/dragonflylee/borealis/commit/0bbc4222d6a7a6e146c69e6acf3521e855cc90fb)、[nlohmann/json 3.11.2 → 3.12.0](https://github.com/dragonflylee/borealis/commit/839f16b2759306f86a0fad0fbe31a5d850d5d660) | 不是单纯小补丁；动画接口改变，内置 Tweeny 的最低 CMake 由 3.0 提高到 3.23，C++ 仍为 17 |
| 输入法 | [新增密码输入接口](https://github.com/dragonflylee/borealis/commit/ccf268521f3c221c20b115f11b2aca2d0fd879e4)与[软键盘改动](https://github.com/dragonflylee/borealis/commit/335c786d6bcb02fc3d473d4ec651202b14584350) | 本项目当前不依赖密码输入；Switch 使用 swkbd，内建字母键盘的启用在 TRIMUI 条件下，不能当作 Switch 新键盘收益 |
| 桌面预览 | [修复 SDL 手柄断开事件的 instance ID 与句柄释放](https://github.com/dragonflylee/borealis/commit/e74acd9e7cfcbe22e763efc906eca5d174c90a5b)；[CJK 字体回退改用资源字体](https://github.com/dragonflylee/borealis/commit/a2833426c21c7bc82a5f7f5b76eff2832e938dc5) | SDL 修复适用于相应桌面路径，不是 Switch libnx 手柄修复；桌面中文预览需准备 chinese_fallback.ttf 等资源，不能依赖旧系统字体搜索 |
| 依赖获取与其它平台 | [SDL/GLFW submodule 改 FetchContent](https://github.com/dragonflylee/borealis/commit/7d7eb3a4e9bca6abf30575233e3bacfb0336531d)；[PS4 持续绘制修复](https://github.com/dragonflylee/borealis/commit/b298947c6d2f21ad65fb6969cc332268069f2a1f) | 桌面默认会下载依赖，GLFW 的 switchfin 分支 URL 未固定提交/校验；PS4 改动不提供 Switch 收益 |

两个快照的 Switch 手柄/触屏输入、共享字体与 deko3d 显示实现、GLFW 显示实现、NanoVG、
Yoga、theme/style 和公共控件 XML 均相同；LICENSE/NOTICE 也相同。Switch 平台源码的差异
限于密码输入，OpenGL 路径的链接库也相同。因此选 dragonflylee 不是因为另一套 HOS 皮肤、
更清晰的 Switch 字体或已知更快的渲染；前文的触屏采样、console 切换等问题对两者同样存在。

**决定采用 dragonflylee/switchfin 的固定提交作为迁移基线**：新增的隐藏容器焦点、
焦点栈清理与工具链改动有直接价值，且不需要另维护 xfangfang 加精选补丁的组合。
此前仅按 fork 归属将其列为参考的判断，经实际 diff 核对后更新；分支名不构成应用耦合证据。

接受的成本与接入约束：使用该提交自带的 Tweeny 4.1.0，CMake 至少 3.23；
不混入旧版 `EasingFunction::...` 示例，新动画使用 `tweeny::easing::...`，自定义 IME
需实现新增的纯虚 `openForPassword`。桌面默认 GLFW 依赖须另固定到提交并校验，
不能只固定 Borealis 后继续下载漂移分支；这些 FetchContent 改动不在 Switch 的构建分支，
Switch 仍使用 devkitPro portlibs。验证动画完成/重置、隐藏区焦点、页面清理、中文预览与
构建可重复性，再决定是否扩大迁移；选型本身不代表构建或实机验收已通过。

### 迁移收益与具体问题

以下保留选型时的迁移判断；首轮实际接入及验证状态以上文为准。

- **可见收益**：现有字体使用 1-bit 阈值、页面按需重画；抗锯齿文本、焦点动画、页面切换
  和统一导航有改善空间。须比较中文密笔画、小字、手柄反馈与切换效果，不能仅凭 GPU 加速
  判定提升，也不为迁移自动增加滑块或确认对话框。
- **C/C++ 边界**：保留 IPC、配置、语言加载和玩法核心的 C 实现，用明确的 C 接口接入
  C++ 页面层，核对函数声明的 C linkage；不把现有 C 文件全部改用 C++ 编译。Borealis
  README 要求 C++17 并移除禁用 RTTI/异常的选项，与当前 NRO 构建不同；Aether 自身
  Makefile 使用 C++17 且禁用 RTTI/异常，不能把一套编译选项套到所有库。
- **页面循环与会话**：`nro/source/main.c` 各页有独立循环，需改成框架调度下的页面状态
  与更新逻辑。框架动画、切页、弹窗或阻塞调用不能中断 IPC 轮询、日志读取、玩法更新和
  停止输出。复用唯一 `Service*`，BLE 保持跨页存活，退出仍执行停止与清理；传输仍归 sysmodule。
- **输入**：框架也会配置并读取手柄，须统一配置、采样与按键分发，避免焦点导航和强度调整
  同时处理同一按键。保留 B 返回、普通页不响应 +、console 用 + 退出，以及现有夹紧/连发语义。
  fork 的 [触屏实现](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/lib/platforms/switch/switch_input.cpp)
  读取单份快照并转换坐标，可枚举多个触点，但不能替代本项目的历史采样和 Start/End 边沿处理。
  玩法继续通过 `platform/touchscreen.c`，避免框架重复采样或用 UI 手势代替玩法输入；
  框架默认触屏路径也需适配现行“只在玩法页读取”的边界。
- **显示与字体生命周期**：框架与 console 都会使用默认窗口，不同时持有两个呈现后端。
  底座切换、console 往返和 GPU/字体资源释放须统一管理，避免原回调和框架回调重复重建。
  共享字体服务与内存也要明确所有者；验证简中回退、语言切换及字形缓存。启动缺语言/服务
  和显示失败时的英文诊断应继续独立于框架 UI；不能假定 GPU 初始化失败都能正常返回并回退。
- **特殊绘制与滚动**：QR 和触屏玩法区需自定义控件。QR 编码沿用已有实现，显示保持整数
  物理像素模块、静区和无平滑过滤。日志、About 长文本与两栏页面按新字体重新测量、滚动与
  裁剪；回收列表中的动态字符串须有稳定存储，不能引用已失效或被复用的缓冲。
- **本地化与交付成本**：继续使用现有 SD 语言包、缺 key 回落和配置目录，不直接改用
  框架内置的翻译/存储系统。核对 UI 资源的 RomFS 打包、子依赖许可证和构建方式，保留根
  VERSION 与现有发布布局。本机和 CI 的依赖可用性、NRO 大小、启动时间、CPU/GPU 内存
  及 Applet 模式预算均待测；完整框架的持续绘制成本不能由现有按需重绘表现推算。

### 后续验证门槛

后续若启动迁移，使用已选定的 dragonflylee 提交并固定子依赖，验证 OpenGL demo 在当前 devkitA64/libnx
及 CI 环境可构建，再将菜单、About、参数列表作为最小验证范围。保持 C 业务核心，
比较两语言、两主题的文字清晰度、长文本滚动、焦点移动、按键提示和返回行为。

扩大迁移前必须验证 QR 扫码、体感/多点触屏输入、强度调整与停止、BLE 跨页存活和退出清理，
以及 720p/原生 1080p、反复底座/console 切换、语言/主题切换和服务缺失时的诊断。
记录启动时间、内存峰值、帧时间和业务更新间隔，并注明实际 HOS、启动模式与依赖版本。

现有 `tests/canvas` 可继续验证保留的自绘代码，但不能证明框架页面或字体正确；
新页面需补截图与交互验证，`tests/lang`、`tests/qr` 和玩法核心测试继续保留。
只有出现明确可见收益、核心行为通过验证且资源成本可接受，才建议全面迁移；
本次未改变当前 framebuffer 实现与页面规则。

## deko3d 后端评估（已放弃）

2026-10-08 决定放弃独立呈现后端迁移：保留 CPU canvas、布局与字体，只将最后呈现
换成 deko3d，没有显而易见的观感或交互变化，却增加拷贝、同步、资源管理与失败诊断成本。
不再保留恢复实施或切换默认后端的路线图。

保留的技术依据来自 2026-09-19 对 deko3d 0.5.0 与 libnx 的核对：交换链按格式表配置
tiled NvGfxFormat，不能直接呈现线性画布，须先 GPU 拷贝到 tiled 交换链图像。
共享画布需同步，按槽独立画布会增加内存；底座/console 切换仍需重建和验证，
deko3d 内部 RaiseError 也不保证返回失败供 console 回退。当时仅官方 deko_basic
本机构建通过，不代表本项目后端已实施或实机可用；旧实现草案留在 Git 历史。

该决定针对独立后端迁移，不排除候选 UI 库内部采用 deko3d。整套 UI 的布局、字体和交互
收益另行评估；Borealis 现代上游的 Switch 路径使用 deko3d，而选定的 fork 也提供 OpenGL。

## 验证入口与待验

`make -C tests/canvas` 覆盖两语言×两主题×720p/1080p 的区域与布局；
`make -C tests/lang`、`make -C tests/qr` 验证语言及编码。预览命令见
`tests/canvas/tools/render_preview.c` 文件头，可设 PREVIEW_THEME/PREVIEW_LANG。

主机图片只能验证布局与像素；仍需实机确认底座真实原生 1080p、无重绘帧画面保持、
console 来回切换稳定、二维码成功率、共享字体的可读性及手柄提示。
缺服务/显示建立失败时的 console 诊断路径已有实机基础，不能让错误信息依赖可能黑屏的后端。
