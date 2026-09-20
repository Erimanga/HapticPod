# 蓝牙能力核对与测试规划

实施进展：BLE 基础测试、PAN + HID 联网、MQTT 回环测试现已实现。当前决策与实测边界见 [接手记录](agent-handoff.md)，功能说明见 [BLE](ble-testing.md)、[PAN](pan-testing.md) 和 [MQTT](mqtt-testing.md)。下文保留实施前的能力核对与完整路线，不能用其中“未启用”的历史状态代表当前固件。

核对日期：2026-09-16。目标：`sf32lb52-lchspi-ulp`，本机 SiFli SDK v2.5.1，LVGL v9 的 `peripheral_lab`。
本次仅核对源码、配置与官方资料，没有开启无线功能、修改固件配置或进行蓝牙实测。

## 1. 当前结论

- SF32LB52 系列支持 Bluetooth 5.3 双模（BLE 与 BR/EDR）。不能据此推断所有 Bluetooth 5.3 可选功能均支持。
- 当前生成的 `.config` 明确为 `# CONFIG_BLUETOOTH is not set`；`CONFIG_AUDIO_BT_AUDIO` 也关闭。现有 MIC/WAV/扬声器测试都是本地音频。
- 目标配置只确定 SF32LB52X 系列，未确定实物完整芯片后缀；涉及新版芯片专属能力前需要核对实物型号。
- 下表的“支持”指芯片/SDK 有依据，尚不等于本项目已移植并通过硬件验证。

| 功能 | v2.5.1 中的依据 | 本项目安排 |
|---|---|---|
| BLE 广播、扫描、连接、断开 | `example/ble/peripheral`、`central_and_peripheral`，文档全平台 | 第一阶段 |
| BLE GATT Server/Client，读、写、通知 | `central_and_peripheral` 源码及 README | 第一阶段；先 server，后 client |
| 配对、绑定、重连 | `example/ble/pair`、connection manager | 第一阶段 |
| BLE 扩展广播、2M PHY、周期广播 | `central_and_peripheral`、`periodic_adv` | 扩展测试；默认传统广播/1M，兼顾手机兼容性 |
| BLE HID 键盘/媒体控制 | `example/ble/hid`，文档全平台 | 后续；需电脑/手机配合 |
| BLE 标准服务、iBeacon、ANCS/AMS | `basc`、`diss`、`iBeacon`、`ancs_dualcore` 及 Kconfig | 可选学习项目；ANCS/AMS 需要 Apple 设备 |
| 经典蓝牙 SPP 数据收发 | `example/bt/spp`，支持 52 系列 | 第二阶段；Android/电脑，普通 iOS BLE 工具不适用 |
| A2DP Sink + AVRCP | `example/bt/music_sink`，支持 52 系列 | 第二阶段；手机音乐 → 板载喇叭 |
| A2DP Source | `example/bt/music_source`，支持 52 系列 | 后续；板上音频 → 蓝牙耳机/音箱 |
| HFP HF 通话 | `example/bt/hfp`，支持 52 系列 | 第二阶段；手机作为网关，板上 MIC/喇叭通话 |
| PAN、音频中继、多连接 | `example/bt/pan`、relay、multi_connect 等 | 进阶，先验证基本链路及资源余量 |
| BLE 语音对讲 | `example/ble/talkback` 明确列出本板；周期广播 + Opus | 后续优先；需要至少两块板；并非标准 LE Audio |
| LE Audio 广播接收/发送 | `example/zbt/bap_broadcast_sink` 及 src 示例 | 独立验证；接收文档列 52x，发送要求 52j/52d 等字母后缀，明确不适用 525 |

LE Audio 使用 Zephyr Bluetooth 示例路径。是否能与本项目采用的 SiFli host、经典音频配置共同构建并稳定共存，需另行验证，不能仅打开若干开关就承诺兼容任意手机的 Auracast。Coded PHY/测向等未完成本板证据核对，不列入本轮支持承诺。

## 2. 第一阶段：BLE 基础测试

入口增加“Bluetooth”分类，每项独立卡片，页面顶部显示协议栈状态、本机名称/地址及连接状态。

| 测试页 | UI 操作 | UI 反馈与通过条件 |
|---|---|---|
| 初始化/信息 | 初始化、查看状态 | 收到栈就绪事件后显示 Ready；不能将函数返回成功当作就绪 |
| 扫描 | 扫描 10 秒、停止、选择设备 | 名称、地址及地址类型、RSSI、广播类型；按地址+类型去重，最多 30 项；收到指定测试设备广播才验证接收链路，环境无广播属于未验证 |
| 广播/被连接 | 开始广播、停止、断开 | 手机发现 `SiFli-Lab-xxxx` 并连接；连接事件自动确认建立链路；仅本地启动广播不能证明手机已收到 |
| 数据收发 | 发测试包、回显开关、清空统计 | 自定义 128-bit 服务：RX(write)、TX(notify)、状态(read)；先短包验证，再根据协商 MTU 分片；显示双向字节数、最近数据及序号 |
| 回环校验 | 发送带序号/校验的挑战包 | 配套手机/第二块板原样回送，匹配后自动 PASS；没有回送端则只显示已发送，不能冒充通过 |
| 配对/绑定 | 开始配对、显示/确认配对码、断开重连、清除本项目绑定 | 显示实际加密、绑定状态与失败码；必须从 SDK 完成事件判断，不能以“已连接”替代“已加密” |
| 主机/GATT Client | 从列表连接、发现服务、读测试值、订阅通知 | 第一版仅操作指定测试服务；不向任意陌生设备写数据。显示服务 UUID、返回值、通知计数 |
| 链路参数 | 查询 MTU/PHY/连接间隔/RSSI，尝试 1M/2M | 显示实际协商结果；对端不支持时明确显示，不判成板子故障 |

手机可以使用支持 GATT 的 BLE 调试工具；回环校验需要工具支持回送脚本或第二块运行配套固件的板。扫描不能保证在任意环境发现设备。

默认只允许一个连接、一种主动测试模式。广播/扫描操作有超时，退出页面停止该测试并完成清理；全局 STOP 一直可见。绑定是持久状态，退出页面不自动删除，显式清除才删除。

## 3. 第二阶段：经典蓝牙与音频

- **SPP**：可发现/连接/断开、TX/RX/回显，显示字节数、序号及校验结果。配对成功和数据传输成功分别展示。
- **手机音乐（A2DP Sink）**：手机配对并播放音乐，显示连接、Streaming、接收进度/可用统计；板上提供音量、播放/暂停。协议事件验证链路，人耳确认音质。AVRCP 控制是否生效由响应/状态变化确认，不能只看按钮已点击。
- **蓝牙通话（HFP HF）**：先测服务连接，再由用户在手机发起受控测试通话；显示通话与 SCO 音频链路状态、上/下行音频状态。UI 可接听/挂断，默认不自动拨号。需要用户确认双方都听得到。
- **A2DP Source**：后续复用内置 WAV，发送给指定耳机/音箱；需处理格式、重采样和音频路由，不能直接把现有强制扬声器的 `audio_open2` 当作蓝牙输出。
- 本地录音、WAV、测试音与蓝牙音频互斥；进入音频测试先释放旧客户端，退出后断开/停止流并恢复有效音量，防止后台抢占 MIC/喇叭。

## 4. 模块划分与线程模型

建议新增（本次尚未创建实现文件）：

```text
src/bluetooth/lab_bt_service.c   栈初始化、事件队列、连接与资源生命周期
src/bluetooth/lab_bt_model.c     设备列表、统计与线程安全快照
src/bluetooth/lab_ble_scan.c    扫描/广播
src/bluetooth/lab_ble_gatt.c    GATT Server/Client 与回环校验
src/bluetooth/lab_ble_pair.c    配对/绑定
src/bluetooth/lab_bt_spp.c      SPP
src/bluetooth/lab_bt_audio.c    A2DP/AVRCP/HFP 与音频互斥
src/lab_bt_ui.c                LVGL 页面、设备列表、配对提示
```

保持现有 LVGL 单线程规则：SDK 回调只复制必要数据入有界队列，不直接操作 LVGL，不阻塞等待用户输入；专用蓝牙任务维护异步状态机，UI 定时读取快照。不能用现有单次测试 worker 一直阻塞等待手机连接，否则会影响其它测试调度。

统一处理超时、用户取消、对端断开、重复连接、队列满及过期回调。每轮测试带会话编号；离开页面后旧事件不能更新下一轮结果。区分通过、失败、等待对端、未验证/不支持，不把所有情况都映射成 FAIL。

页面“LEARN THE API”展示经本地源码确认的调用：

- 广播：`sibles_advertising_init/start`。
- 扫描/连接：`ble_gap_scan_start/stop`、`ble_gap_create_connection`、`ble_gap_disconnect`。
- GATT：`sibles_register_svc_128`、`sibles_register_cbk`、`sibles_write_value`、`sibles_search_service`、`sibles_read_remote_value`。
- 经典蓝牙事件：`bt_interface_register_bt_event_notify_callback`；等待 `BT_NOTIFY_COMMON_BT_STACK_READY`。
- BLE 初始化完成：`BLE_POWER_ON_IND`。

API 签名、对象寿命及返回事件按本机 v2.5.1 头文件接入，不直接照抄最新在线 SDK。

## 5. 配置与资源检查

第一阶段候选开关：`BLUETOOTH`、`BSP_BLE_SIBLES`、`BLE_GAP_CENTRAL`、`BLE_GATT_CLIENT`、`BSP_BLE_CONNECTION_MANAGER`、HCPU 的 `BSP_BLE_NVDS_SYNC`。绑定持久化参照示例的 FlashDB 配置，验证板级 `KVDB_BLE_REGION`，不能覆盖其它分区。

第二阶段按需启用 `BT_PROFILE_CUSTOMIZE` 与 `CFG_SPP_SRV/CLT`、`CFG_AV/CFG_AV_SNK`、`CFG_AVRCP`、`CFG_HFP_HF` 和 `AUDIO_BT_AUDIO`，检查生成配置及隐式默认启用的 profiles。每步先编译基本示例/集成固件再扩大功能，不一次启用所有协议。

当前 WAV 版本编译基线：ROM 947,978 B；静态 RAM 237,488 / 523,264 B。剩余静态容量不是运行时可用堆保证；麦克风还有按需分配的 96,000 B 录音缓冲。开启蓝牙后必须重新检查 LCPU 镜像、IPC、ROM/RAM、线程栈高水位、空闲堆及峰值，验证 LVGL 刷新与音频 DMA 并发。已有 PSRAM 不代表协议栈所有缓冲都能搬入其中。

## 6. 验收顺序

1. 编译：BLE 最小集 → GATT/配对 → 经典数据 → 经典音频；保存每步容量差异。
2. 主机测试：扫描去重/上限、数据长度及 MTU 分片、包校验、超时与取消、过期回调过滤。
3. 单板+手机：广播连接、双向收发、关闭手机蓝牙、拒绝配对、断开重连、页面退出与 STOP。
4. 双板：确定性回环、1M/2M（两端能力允许时）、连续传输与重连统计。
5. 音频：A2DP、HFP 及本地 MIC/WAV 互斥；运行时内存与 UI 流畅度回归。
6. 再安排 HID、BLE 对讲与 LE Audio 独立验证。

本次规划完成后尚无蓝牙编译、吞吐量、距离或硬件通过结论；烧录与实测另行执行。

## 7. 资料依据

本机 SDK 根目录：`/Users/ljp/Desktop/dev/sifli/SiFli-SDK/v2.5.1`。主要核对 `middleware/bluetooth/Kconfig`、上述示例的 README/源码，以及目标板 `hcpu/board.conf`、`ptab.json`。

- [官方 SF32LB52 硬件说明：双模 Bluetooth 5.3](https://wiki.sifli.com/en/hardware/SF32LB52B-E-G-J-HW-Application.html)
- [本板官方说明](https://docs.sifli.com/projects/sdk/latest/en/sf32lb52x/supported_boards/boards/sf32lb52-lchspi-ulp/doc/index.html)
- [经典蓝牙示例目录](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/example/bt/index.html)
- [LE Audio 广播接收示例与发送端型号限制](https://docs.sifli.com/projects/sdk/latest/en/sf32lb52x/example/zbt/bap_broadcast_sink/README.html)

在线页面为 latest，仅作辅助交叉核对；以上规划以本地 v2.5.1 内容为准。
