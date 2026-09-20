# Peripheral Lab 接手记录与已确认经验

记录日期：2026-09-16。适用范围：本仓库 `peripheral_lab/` 与这次会话的后续工作。

这份文档保存已经核对的事实、取舍、验证边界和查阅入口。无需每次重新搜索整个 SDK；代码、SDK 版本或实测现象变化时，再针对相关结论复核。Git 状态、临时文件及公共服务可用性必须按需更新。

## 1. 一分钟了解当前状态

- 平台：SiFli SDK **v2.5.1**，板型 **sf32lb52-lchspi-ulp**，LVGL **v9**，390 × 450 屏幕。
- 项目：根目录 `peripheral_lab/`。更早的 `lvgl/watch`、`lvgl_v8_demos`、ws2812/gpio 升级属于历史任务，当前操作目标不是那些目录。
- UI：17 个测试入口（新增 Play Lab 综合测试，首个游戏为鹈鹕自行车），各外设模块独立文件；蓝牙页与联网页独立 overlay。
- 经典蓝牙最终选择：**PAN + HID**。A2DP Sink 是已验证的备选，不是 iPhone 上网必需条件。
- MQTT：联网页已有 **Test MQTT**，公共匿名 Broker `test.mosquitto.org:1883`，一次性 QoS 0 发布/订阅精确回环；尚无板端 MQTT 实测反馈。
- 用户最后的烧录约束：**先编译，暂不烧录**。后续用户明确要求烧录可覆盖此约束。
- 文档中提到的 PASS 必须附带验证层次，不能将模拟 UI 数据作为上板成功证据。

### Git 基线

| 提交 | 用途 |
| --- | --- |
| `196f01b` | 外设测试交互、传感器和音频验证基线 |
| `a196a20` | BLE 基础测试与主机验证 |
| `7414f46` | BTPAN + A2DP Sink 辅助连接；保存 iPhone 已通过的备选方案 |
| `a010ec4` | 保留 HID 辅助 PAN；记录两种方案的用户实测结果 |

本文写入时 HEAD 为 `a010ec4`，MQTT 实现和本次文档整理仍在工作区，未提交。**先执行 `git status --short` 和 `git log -4 --oneline`，不要假设以后仍如此。** 用户之前设置的 Git 作者是 `xyz <xyz@nowhere.com>`；无须反复询问。没有推送要求。

## 2. 哪些已经验证，哪些没有

| 项目 | 证据来源 | 已确认结果 / 限制 |
| --- | --- | --- |
| PAN + HID / Android | 用户上板测试 | 收到 HTTP 200 |
| PAN + A2DP Sink / iPhone | 用户上板测试 | 初次重启 iPhone 后发现、配对并收到 HTTP 200；随后无需重启重复通过 |
| 恢复 PAN + HID / iPhone | 用户上板测试 | 无需再次重启手机即可正常配对联网 |
| MQTT 编码/解析 | 生产模块主机测试 | CONNECT、SUBACK、精确回环、分包/粘包、异常长度/ACK、retained 拒绝等通过 |
| MQTT 网络生命周期 | 生产 `lab_pan_net.c` + 传输替身 | 部分发送/接收、回环、HTTP 回归、STOP、路由恢复、迟到 DNS、超时、IP 丢失通过 |
| MQTT 公共 Broker | 桌面真实网络 + 生产协议模块 | `test.mosquitto.org:1883` 实际回环通过；不是板子或 PAN |
| MQTT / 板端 PAN | 尚无用户反馈 | 待上板测试，不能写成已通过 |
| LVGL 图像和交互 | 真实 LVGL 源码 + 模拟硬件 | 软件渲染及注入交互验证；不是屏幕截图，不是 MCU 全系统模拟器 |
| 最新目标固件 | SDK v2.5.1 构建日志 | 编译链接通过；不等同于 RF、Flash 持久化、音质或长期稳定性验收 |
| BLE 基础功能 | 编译、UI、状态机主机测试 | 具体范围见 BLE 文档；没有足够用户反馈支持全面上板验收结论 |

最新 MQTT 目标构建统计约 ROM 1,575,009 B、静态 RAM 305,584 B。数值是该次配置的快照，不包含全部运行时堆分配；需要比较资源变化时重新读取构建结果。

## 3. iPhone 排查经验：不要重复得出错误结论

### 已发生的顺序

1. 单 PAN 初版：iPhone 提示不支持/配对失败，板端 Phone disconnected。
2. 加入 HID 后：iPhone 一度不显示设备；Android 同版能完成 HTTP 200。
3. 日志确认 Begin 后名称正确且 `Scan mode: 3`，不是“没有打开可发现模式”。
4. 换 A2DP Sink 后首次仍未显示，重启 iPhone 后成功，并可重复连接。
5. 换回 HID 后，不重启 iPhone 也可配对联网。
6. 用户决定先保留 HID，并要求提交；已落实到 `a010ec4`。

**结论：两种方案均在用户的 iPhone 上验证可用。** 之前现象更符合手机临时状态/缓存或设备声明变化与缓存的交互，但没有 HCI 对照证据确认具体机制。Profile 与手机重启同时变化过，不能据此证明 A2DP 修复了问题，也不能断言板端完全无关。不要要求正常重连每次重启手机。

### 文档与本地示例存在差异

- 官方 PAN README 写的是“PAN & A2DP”，用于 iOS 单 PAN 的兼容处理。
- 本机 v2.5.1 的 `example/bt/pan/project/proj.conf` 实际启用 `CFG_HID`；`src/main.c` 设置 `BT_CM_HID`，并在 HID 连接后连接 PAN。
- 因此“文档写 A2DP”不能推导出“HID 一定不能用”。以源码配置和目标手机实测分别说明。
- 当前选择 HID 是因为以联网为主，不需要成为手机媒体输出；并不是已证明 HID 更快、更省电或跨 iOS 更稳定。

### 当前方案与备选的差异

| 内容 | 当前 HID | A2DP 基线 `7414f46` |
| --- | --- | --- |
| Profile | PAN + HID | PAN + A2DP Sink |
| 设备类别 | Networking + LAN Access Point，低 24 位 `0x020300` | Networking + Audio + Rendering + Loudspeaker，`0x260414` |
| EIR | `set_local_name` 写名称，与此前 HID 版本一致 | 更名后追加完整名称、Audio Sink `0x110B`、PANU `0x1115` |
| 附带行为 | SDK 原有自动鼠标校准被链接包装屏蔽 | 蓝牙音乐播放关闭，手机仍可能把媒体输出切过去 |
| 启用音频播放 | 否 | 否；只是 Profile 发现/连接对照，不是蓝牙音箱功能 |

### 关键日志的含义

- 旧固件名称为 `SiFli-Lab-PAN`；2026-09-17 起使用 `SiFli-Lab-XXXXXX`，后缀为本机 MAC 常规显示顺序末 6 位大写十六进制。PAN 页在配对前显示名称和本机 MAC，串口输出 `[lab:pan:identity]`；BLE 名称同步为同样的六位后缀。目标构建/主机验证与板端实测分开，当前新命名未烧录。
- 开机 `Scan mode: 0` 是测试页默认关闭发现的行为。Begin pairing 后开放 60 秒。
- `Scan mode: 3` = Inquiry scan + Page scan 都开启。UI ACTIVE 仅代表测试启用，SCAN 才展示控制器读回状态。
- `set name` 后乱码：SDK `bt_interface_set_local_name()` 用 `%s` 打印含长度/类型且不以 NUL 结尾的 EIR 缓冲区。这是打印问题；不能仅凭乱码判定名称写坏。实际日志随后读回名称正确。
- `read bt bonded failed` 单独一条不能证明协议栈坏了，应结合后续 Stack ready / Profile enabled / 连接事件判断。
- 用户日志已出现 PAN/HID enable success 和 Begin 后 SCAN 3；没有手机连接事件时应排查发现/显示阶段，不先改 DNS 或 HTTP。
- OUI 警告是官方 README 的通用提醒。当前 Android/iPhone 均已联网，**没有证据需要改 MAC**，不套用网上示例地址。

建议收集：手机型号/iOS 版本、固件 Profile/身份配置、点击 Begin 后 SCAN、完整 `[lab:pan:event]` / `pair` / `profile` / `scan` / 断开原因码。原始附件和临时日志可能失效，本文保留关键事实，不依赖附件永久存在。

## 4. 实现地图与不要破坏的约束

| 位置 | 职责 |
| --- | --- |
| `src/lab.h`、`lab_registry.c`、`lab_ui.c` | 测试 ID、入口/说明/API、首页与详情导航 |
| `src/lab.c`、`src/modules/` | 普通外设串行 worker 与独立模块；持续采样可停止 |
| `src/lab_ble_ui.c`、`src/bluetooth/` | BLE UI、专用任务和事件队列、模型校验 |
| `src/lab_pan_ui.c`、`lab_pan.h` | PAN/HTTP/MQTT UI、命令与快照 |
| `src/network/lab_pan_service.c` | 经典蓝牙事件队列、配对同意、HID/PAN 生命周期 |
| `src/network/lab_pan_net.c` | tcpip 回调、DHCP/DNS、绑定 PAN IP 的非阻塞 socket、HTTP/MQTT 网络推进 |
| `src/network/lab_mqtt.c/.h` | 无 RTOS 依赖的一次性 MQTT 3.1.1 协议测试器 |
| `src/network/lab_pan_identity.c` | 当前经典蓝牙设备类别 |
| `src/network/lab_pan_hid_compat.c` | 屏蔽 SDK 自动 HID 鼠标输入；控制 Input 应答中立化 |
| `src/network/lab_pan_audio_compat.c` | 无 HFP/蓝牙音频时的 SDK 音频链接兼容入口 |
| `project/proj.conf`、`Kconfig.proj` | 人维护的配置；不要改 build 下自动生成的 rtconfig.h |
| `project/SConstruct` | Bootloader/HCPU/LCPU/FTAB 构建；LCPU 镜像自动生成 |
| `tests/render.py` | 主机 UI/模型/状态机测试总入口；默认不访问公共网络 |
| `tests/mqtt_probe.py` | 显式运行才连接公共 Broker 的桌面探针 |

约束：

- `main.c` 先 `lab_pan_init()` 注册经典蓝牙回调，再 `lab_ble_init()`；BLE 任务调用唯一一次 `sifli_ble_enable()`。不要每个测试页重复开启/关闭整个共享协议栈。
- SDK 回调只复制有界事件到队列，不操作 LVGL、不阻塞联网。UI 读互斥保护的快照。
- 退出 overlay 必须停止活动；generation 防止旧命令和迟到 DNS 回调重新开启测试；断开以完成事件为准。
- 网卡/DHCP/DNS 操作在 lwIP tcpip 线程执行。网络任务非阻塞轮询；不得在 UI 线程阻塞 DNS、connect 或 recv。
- PAN 网卡名称由 SDK 生成 `b0` 等。临时设置默认路由，退出恢复仍存在的旧路由；socket 绑定 PAN IP，避免误用其它网络。
- 当前不自动回连、不清空所有绑定；STOP 保留 bonds。
- `BT_USING_PAIRING_CONFIRMATION` 在项目 Kconfig 自行声明。SDK 不开启此宏时可能自动接受数字确认；保留显式 Accept/Reject 与 25 秒超时。
- HID 包装仍必须链接：`--wrap=hid_send_report_req_ext`。移除会恢复 SDK 的自动鼠标移动行为。不要包装成普通无条件发送。
- `BT_FINSH` 也被 SDK 音频头文件用作语音判断，单 PAN 会触发缺失 `bt_voice_downlink_process` / `bt_voice_uplink_send`。本项目仅在 HFP 和 AUDIO_BT_AUDIO 都关闭时提供空实现。未来开启蓝牙音频必须使用 SDK 实现，不能保留空实现掩盖功能缺失。
- `src/lcpu_img.c` 为生成产物，已忽略；不要提交生成镜像、build 目录或 compile_commands。

### MQTT 当前边界

公共 Broker 已获用户明确选择。`test.mosquitto.org:1883` 明文匿名；只发布合成 probe，不发真实传感器数据或凭据。QoS 0、Clean Session、非 retained、完整主题匹配、不用通配订阅。

通过条件是精确收到本轮 topic/payload 并完成 DISCONNECT 发送，不是仅连接成功或 write 返回成功。处理部分发送、TCP 分包/粘包和超时。HTTP/MQTT 串行，复用 PAN 的网络状态机。TLS、QoS 1/2、长期重连等均未实现，不把它当完整 MQTT SDK。

## 5. 可复用的验证方法与命令

### 工具与环境

优先使用仓库 SiFli CodeKit skill 的构建/串口工具。此前 CodeKit 端点曾多次变化，旧聊天 Bearer token 和 localhost 端口不再作为有效配置。**不要把令牌写入文档。** 本会话已获准在工具不可用时用命令行构建，不需要为同一已授权构建重复请求确认。

本机路径：

- 仓库：`/Users/ljp/Desktop/dev/sifli/lckfb-hspi-ulp_example`
- SDK：`/Users/ljp/Desktop/dev/sifli/SiFli-SDK/v2.5.1`
- 已用过的有效环境脚本：`/var/folders/sp/9hkkcwdn4nn4wmv64br7gd8r0000gn/T/sifli-sdk-export/sdk-env-voje9fdn.sh`
- 上一项是临时文件。先检查是否存在；失效时按 skill/SDK 环境流程重新生成，不能认为构建失败就是源码错误。无需把临时脚本内容提交到仓库。

从仓库根目录执行目标编译（环境脚本仍有效时）：

```sh
source /var/folders/sp/9hkkcwdn4nn4wmv64br7gd8r0000gn/T/sifli-sdk-export/sdk-env-voje9fdn.sh
scons -C peripheral_lab/project --board=sf32lb52-lchspi-ulp -j8 > /tmp/peripheral-lab-build.log 2>&1
```

检查退出码、`scons: done building targets`、error 及内存统计。输出目录是 `peripheral_lab/project/build_sf32lb52-lchspi-ulp_hcpu/`。SDK 已出现过自身的 RWX/newlib syscall/FTAB entry 警告，以及全量构建中的 SDK 源文件警告；不要自动认定新引入，也不能忽略新增应用警告。

### 主机回归与 LVGL 预览

本机默认 `cc` 曾因 Xcode license 阻塞，独立 CLT 编译器可用：

```sh
CC=/Library/Developer/CommandLineTools/usr/bin/clang \
CFLAGS='-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk' \
python3 peripheral_lab/tests/render.py \
  --sdk /Users/ljp/Desktop/dev/sifli/SiFli-SDK/v2.5.1
```

输出 `/tmp/peripheral-lab-preview/` 的 PPM，可转换预览：

```sh
sips -s format png /tmp/peripheral-lab-preview/pan-mqtt.ppm --out /tmp/peripheral-lab-preview/pan-mqtt.png
```

此脚本编译 SDK 真实 LVGL v9 + 项目生产 UI，通过假后端和注入输入运行断言，读取软件帧缓冲生成图像。**没有启动桌面交互模拟器窗口，没有截取板载 LCD，也没有模拟真实蓝牙/传感器。** BLE 状态机测试使用真实 SDK 类型；PAN 网络测试使用替身接口，目标编译另行核对真实 ABI。

可选真实公共 Broker 探针：

```sh
CC=/Library/Developer/CommandLineTools/usr/bin/clang \
CFLAGS='-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk' \
python3 peripheral_lab/tests/mqtt_probe.py
```

它会经桌面网络发送一条合成消息，结果只证明生产协议模块能与该 Broker 交互；不证明 PAN。公共服务状态可能变化，重跑是新的在线测试，不是稳定的离线单元测试。

本次日志参考：`/tmp/peripheral-lab-mqtt-build.log`、`/tmp/peripheral-lab-mqtt-ui.log`、`/tmp/peripheral-lab-mqtt-live.log`。这些路径不保证长期存在；本文保留结论与复现命令。文档修改无需重新编译整套 SDK；代码修改按涉及模块选择检查。

## 6. 更早的外设经验（不要反复回退设计）

- 导航最终要求是**左边缘向右滑返回、底边缘向上滑回首页**，不是任意区域“快速滑动”。中间区域保留滚动和触摸测试。具体阈值在 README 和生产代码中。
- 单点触摸显示命中的 target；双点通过真实控制器帧验证两个不同 ID，不用两个鼠标事件伪造。
- 用户 FT6146 日志为 Chip `64` / FW `05`，最高报告 1 点。只能说该板/固件尚未观察到双点，不能仅靠驱动支持双槽断言硬件支持两点。采集使用 I2C 包装旁路，不新增抢读触摸的第二个消费者。
- 麦克风用户提供型号 **LMA2718T421**；能录音/放音。最新修正读数：30 cm -52 dBFS、20 cm -48 dBFS、10 cm -43 dBFS、2 cm -34 dBFS，安静约 -62 dBFS。此前 10 cm -34 的读数已被用户纠正，不再引用。
- dBFS 是数字电平，不是声压 dB SPL。仅凭 MEMS 类型或这些读数不能确认硬件故障/灵敏度规格；回放加增益同时增噪，不能当成提升输入信噪比的证明。
- 录音 3 秒保留原始 PCM，+18 dB 是可比较的播放增益；八音阶是 C4→C5。扬声器还有音量/频率条及内置 WAV，频率条只用于自定义音调。
- WAV 当前仅支持 PCM、16 kHz、单声道、16 位；内置原创旋律编译进 Flash，不依赖文件系统。资源与生成工具见 README。
- ADC/六轴/三轴/光照支持持续采样，展示物理单位与原始数据；离页停止、恢复配置。UART 分 TX、RX、回环，接线与 console 引脚复用见 README，勿凭目录名猜硬件。
- 用户已反馈独立 ws2812 示例颜色变化正常，早期“始终白色”的猜测不能保留为当前故障。

## 7. 只在需要时查这些资料

下面是已用过的本机 SDK 相对路径，以前面的 SDK 根目录为基准：

| 问题 | 已定位来源 |
| --- | --- |
| PAN 示例和 HID/A2DP 差异 | `example/bt/pan/README.md`、`project/proj.conf`、`src/main.c` |
| A2DP Sink 配置 | `example/bt/music_sink/project/proj.conf`、`middleware/bluetooth/Kconfig` |
| 配对宏、扫描完成事件、默认设备类别 | `middleware/bluetooth/service/bt/bt_finsh/bts2_app_generic.c` |
| 扫描接口、名称/EIR 写入与打印问题 | 同目录 `bts2_app_interface.c` |
| 事件载荷类型 | 同目录 `bts2_app_interface_type.h`：ACL_CONNECTED 是 `bt_notify_device_acl_conn_info_t`，不是只读基础 MAC 的任意结构 |
| HID 自动鼠标校准、报告出口 | 同目录 `bts2_app_hid.c`：`bt_hid_mouse_reset_at_middle`、`hid_send_report_req_ext` |
| Profile 目标设置 | `middleware/bluetooth/service/bt/bt_cm/bt_connection_manager.c`；`set_profile_target` 的 addFlag=0 是替换目标，1 是 OR 添加 |
| PAN 网卡及路由 | `middleware/bluetooth/porting/rtt/bt_pan/bt_lwip.c` |
| 蓝牙音频链接条件 | `middleware/audio/include/audioproc.h`、`audio_manager/audio_server.c` |
| 非阻塞 socket / 异步 DNS | `rtos/rtthread/components/net/lwip/lwip-2.1.2/src/include/lwip/` |

外部权威参考（无需每次通读）：

- [SiFli PAN 官方说明](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/example/bt/pan/README.html)
- [Mosquitto 公共 Broker](https://test.mosquitto.org/)
- [MQTT 3.1.1 规范](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)

项目专题入口：[总说明与接线](../README.md)、[BLE](ble-testing.md)、[PAN](pan-testing.md)、[MQTT](mqtt-testing.md)。`bluetooth-test-plan.md` 是历史能力核对与路线图，其“未开启/计划”不能覆盖当前实现。

## 综合测试游戏（2026-09-16 新增）

- 主页最后一张 Play Lab → Pelican ride，前后/左右倾控制、60 秒收鱼挑战、路障、平衡及暂停/重玩。2026-09-17 按用户板端反馈改为实际屏幕 Y 移动、独立朝向及路径转向、首帧快速居中，移除校准遮罩和 Flip/Swap 按钮。
- 实现与验证边界：[鹈鹕自行车](pelican-ride.md)。原生 LVGL 图元，游戏模型与 IMU worker 分离；退出恢复配置；有独立短音效任务，不操作马达。
- 已目标编译、真实 LVGL 软件渲染和主机模型/传感器替身测试；**本次未烧录，修正后的轴向、手感和板端性能待复测**。初版用户反馈只能横移、缺少纵向/转向反馈，且校准遮挡及按钮过多；不能把初版主机通过当成板端通过。

最新布局为全屏场景（390×450），顶部90px天空含太阳、云和直接绘制的缓慢飞鸟；底部两行悬浮信息显示鱼数/倒计时/生命和速度/里程，旁边48px暂停入口；居中、重玩、退出放入暂停菜单，常驻 Done/Back 已移除，边缘手势保留。Center 点击后自动居中并关闭菜单恢复游戏。背景透明度不使用整对象/离屏层。

新增稀疏绿色补给包（+1 HP，上限5，拾取去重）和暂停菜单三档难度滑条；更改不重置本局，重玩保留，重启恢复普通。模型与UI回归继续检查无额外像素缓冲，板端玩法待复测。

最新造型为自行车，包含车架/车把/鞍座和随距离驱动的双腿踩踏、轮辐动画；停车与暂停冻结，倒退反转。继续使用坐标绘制，禁止对象旋转离屏层。

### 游戏绘图内存故障（2026-09-17）

用户板端进入游戏 hang，重复日志 `lv_draw_buf_create_ex: No memory: 118x126 ... 59472Byte` / `Allocating layer buffer failed. Try later`。已核对 SDK 非零对象旋转会创建 TRANSFORM 图层；鹈鹕整体旋转触发约 59 KB 临时像素缓冲，主机渲染通过不能证明目标堆够用。

修复为同一绘图层中直接绘制旋转端点的圆头线段，禁止给角色/轮辐使用对象 transform_rotation。新增主机 draw-buffer 分配守卫及无离屏层断言，覆盖游戏生命周期。目标编译和主机测试通过，本轮未烧录，修复效果待用户板端复测。不要通过扩大堆或只关闭警告掩盖该路径。

### 游戏轴映射（用户反馈后的修正）

用户反馈按钮在下、屏幕竖起时角色向右，右侧抬起时向下。由旧映射与运动符号推断，游戏输入应先转换为屏幕轴 `(-sensor Y, +sensor X, sensor Z)`，再计算倾角及居中；只改游戏，不改变独立 IMU 测试显示的原始轴。已加入四向模型及 UI 回归，原始三轴未现场采集，修正版方向仍待用户板端复测。

### 游戏音效

新增 `lab_game_sound.c/.h`：出发、收鱼、回血、碰撞与胜负结算短音，独立任务、有界邮箱、generation 取消，不在 UI/IMU 采样线程中播放。游戏菜单独立音量滑条0–15（默认6，0静音，与Speaker设置隔离，重玩/重新进入保留），每次播放后恢复音频服务原音量；退出由 lab_io 等待客户端关闭后释放 busy。音频测试失败不写成游戏失败，具体恢复边界见游戏文档。目标编译与主机音频替身/线程回归通过，本轮未烧录，用户最新板端反馈听感正常；音频堆余量与长期稳定性尚未专门实测。

用户反馈碰撞/结算像“噗噗声”后，已将碰撞改为1046→622Hz下滑，胜利改上行旋律、失败改784→659→523Hz，延长结算尾音，加入余弦起止包络与首尾静音。主机波形检查及编译通过，最终PCM版本已获用户板端听感正常反馈；试听 WAV 由 render.py 输出，不能当板端录音。

用户随后反馈板端声音远短于主机试听、只剩“叮”。本轮增加完整 PCM 时间线与下游200 ms收尾保护和每段提交/取消/错误日志，补充下游延迟模拟回归。当前目标未启用软件混音，不能将混音缓存截断直接认定为板端根因；该版用户复测仍短音。最新日志收鱼260 ms却耗4010 ms并正常结束，转向供数过慢排查：将逐采样三角函数改为Flash PCM（82,880 B），送音优先级23→18（绘图19），新增送入耗时/最大间隔日志。生成器一致性、DMA背压模拟、主机回归和目标编译通过（ROM 1,672,838 B、静态RAM 306,408 B）。用户最新反馈“这次听感正常了”；附带日志 start 260/260 ms、feed 211 ticks、总492 ticks，hit 250/250 ms、feed 210 ticks、总481 ticks，均done（1000 Hz tick）。这是用户自行板测确认，本代理未烧录。详见游戏文档。

## 8. 后续工作的最短路径

1. 读本页当前状态，检查 Git 工作区；只读与新任务相关的专题和模块。
2. 当前最直接的待验收项：用户在板子取得 PAN/IP 后点击 Test MQTT，确认完整回环。没有反馈前保持“板端 MQTT 待验证”。
3. 遇到失败按发现 → 配对/ACL → HID/PAN → IP → DNS/TCP → HTTP/MQTT 分层定位，不一次更改多项配置后宣布单一根因。
4. 新实测结果写入对应专题及本页验证表，标明用户反馈还是工具实测；修正过时的“待验证”文字，不只在末尾追加互相矛盾的记录。
5. 若用户要求切换辅助 Profile，优先参考现有提交；保留当前改动，不用强制 reset 来恢复旧版本。
