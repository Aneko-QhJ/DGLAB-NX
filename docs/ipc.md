# IPC 接口

IPC 是 NRO/Overlay/Game Mod 使用 sysmodule 的内部公共 API。完整命令号、请求/回复类型、
字段范围和常量以 [ipc.h](../common/include/dglab/ipc.h) 为准；客户端不得复制定义。
服务端分派见 `sysmodule/source/main.c`，NRO 调用示例见 `nro/source/main.c`。

## 基本约定与版本

- 服务名 `dglab`；同进程子页复用一个会话，目前 sysmodule 一次只服务一个 IPC 会话。
- 已发布命令号不得重排，新命令用新号。临时 PoC 接口在
  [ipc_poc.h](../common/include/dglab/ipc_poc.h)，不属于稳定契约。
- 回复需放进 0x100 字节 IPC 缓冲区，超长数据分块；主机测试使用 libnx CMIF 编码请求，
  验证客户端发送与服务端解析一致，入口 `make -C tests/ipc`。
- GET_VERSION 返回头文件中的接口版本，按 `0xMMmmpp` 打包；发行版本来自根 VERSION，
  与此无关。不兼容变更递增次版本号。

接口沿革：0.1.0 基础命令，0.2.0 NET_*，0.2.1 BLE_*，0.2.2 BLE_LIMIT，
0.2.3 将 BLE_START/BLE_LIMIT 上限改为 A/B 两个值。

## 命令分工

| 命令组 | 语义 |
| --- | --- |
| GET_VERSION / PING | 核对接口版本与服务身份 |
| NET_START / NET_STOP | 启停 WebSocket 服务端；不会开机自启，停止会断开所有连接 |
| NET_STATUS / NET_QR | 状态快照与二维码；二维码有局域网地址即可生成，不能据此判断服务端运行 |
| NET_SEND | 强度、清空与短测试指令；事件源的实际波形用 NET_WAVEFORM |
| NET_WAVEFORM | Append 连续流；Replace 独立事件；连续波形需持续上传，由 sysmodule 补流 |
| NET_LOG | 游标式增量读服务端日志；从 0 开始，以 next_cursor 继续，size=0 表示追平；落后则夹到环中最旧可读位置 |
| BLE_START / BLE_STOP / BLE_STATUS / BLE_LIMIT | 建立、停止、查询 BLE 会话及运行时修改两个通道上限 |

NET_STATUS 顺带刷新局域网地址。App 上报字段只代表收到的报文，不能用初始 0 推断设备真实值；
实测 App 3.0 不上报，见 [Socket 文档](dglab-socket.md)。
失败用 libnx Result 返回；检查 R_FAILED，具体失败同时查看状态与日志。
未绑定/无地址、无效输入、监听/写失败分别对应 NotFound、BadInput、IoError，详见实现。

## BLE_*（sysmodule 直连设备）

BLE_START 带设备地址与 A/B 通道上限，地址由扫描写入 `config/dglab-ble-address.txt`，
NRO 读出后传入。上限和强度是独立的两组 A/B 值：前者是 BF 设备钳制值，后者是客户端请求值。
NRO 先运行驱动级探针，再发 BLE_START，由 btm 连接。POC_START 的可选
`SHORT_OBSERVATION` 标志只将找到目标后的观察窗口从 20 秒缩短到 1 秒；
原有初始化、注册、两轮扫描和诊断调用顺序保留，诊断入口不带该标志。
POC_STATUS 的 `DRIVER_READY` milestone 要求开栈、启用、扫描配置/启动、最终停止
和清理成功；NRO 正式入口还要求第二轮实际发现目标。HOS 22.5.0 实测第一轮
StopBleScan 可返回 0x0002A871，仅当后续目标扫描和最终停止成功时允许恢复该返回值；
其它关键错误不恢复，取消不置位；诊断性客户端注册/连接失败不作为准备失败。
NRO 驱动准备每次开机只运行一次；准备失败/取消后拒绝再次启动，须重启。NRO 仅在状态 Stopped、
last_result=0 且 DRIVER_READY 置位时发送 BLE_START；状态读取失败或准备失败即终止。
命令号和请求/回复布局不变；新版 NRO 遇到旧服务缺少标记时拒绝继续，需一起更新。
遇到 0x668F 或连接请求受理后超时，拒绝本次开机的后续 START；下一轮必须重启。
旧工作线程尚未退出时 START 返回 Kernel/TimedOut，客户端后续帧重试；
STATUS/LOG 保持可用，PoC 日志跨工作线程保留未读尾部。
BLE_LIMIT 用于运行中改上限，避免停会话重连；无会话时应由 BLE_START 带入。

会话激活时 NET_SEND 的强度请求及 NET_WAVEFORM 路由到本地协议层；BLE_STOP 后回到
Socket 转发。玩法不需要另建传输接口。BLE 的 Clear 清除指定通道波形（channel=0 为两路），
保留连接和强度设置；TestPulse 仍被忽略，测试使用 NET_WAVEFORM 加 SetStrength。
NET_WAVEFORM 按通道处理：Append 追加未播槽，Replace 替换对应通道；每批仅播放一次。
IPC 暂存每路最多 48 槽，协议待播队列每路最多 128 槽；暂存满则拒绝请求，
工作线程待播队列满则记录拒绝日志。不能仅因 NET_* 名称相同就假定两种传输全部等价。

BLE 按开环设计：BLE_STATUS 的强度是请求记账，不能显示为真实强度，不支持依设备当前值加减。
正式会话连接后仅写 BF 和一次 A/B 归零，不自带波形；无波形或待发强度请求时不发 B0。
测试/玩法波形按 100ms 节奏发送，播完回到空闲；强度请求用序列号 0，不等待 B1。
STOP 先写 BF=0，再清波形归零后断开。
使用前必须安装补丁、确认设备未绑定，并遵守一个开机周期一条 BLE 路径，详见
[BLE 验证](ble-poc.md) 与 [固件研究](ble-re.md)。

## 波形与通道强度

通道强度由用户设置，事件源只产生波形值和频率，两者在设备侧共同决定输出。
槽位应按 4 的倍数上传；不足一组的余量等待后续补齐。队列溢出丢最旧内容并记日志，
Clear 同时清空 Socket 队列。Replace 的 socket 发送由 tick 完成，不在 IPC 上传时阻塞写。

TestPulse 是短链路测试，不是游戏事件流接口；NRO 的 ZL/ZR 测试通过波形上传与通道强度请求
组合完成。槽位范围、批次/队列上限和测试素材查公共头文件及 `net_server.c`，不在此重复维护。
