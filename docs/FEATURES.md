# 功能与代码入口

[中文](FEATURES.md) · [English](FEATURES.en.md)

先运行完整板端应用；移植新 BSP 时使用独立 managed 示例。每个 snippet 都加入 CMake 编译，但不是默认全部开启。SDK 内部实现在闭源静态库中。

| 功能 | 简化调用示例 | 板端实现 | 状态与客户工作 |
| --- | --- | --- | --- |
| 启动、生命周期、错误处理 | [quick_start.c](../examples/managed-terminal/snippets/quick_start.c) / [api.c](../examples/managed-terminal/snippets/api.c) | [main/terminal.c](../main/terminal.c) | 根工程已接板；标准入口含退出与队列重试 |
| 设备授权与绑定 | [license.c](../examples/managed-terminal/snippets/license.c) | [main/factory_license.c](../main/factory_license.c) / [main/factory_usb.c](../main/factory_usb.c) | 需要本设备授权与可信公钥，NVS 持久化 |
| Wi-Fi、PIN、重连 | [bluetooth.c](../examples/managed-terminal/snippets/bluetooth.c) | [main/terminal.c](../main/terminal.c) | TCP 5004 主连接、UDP 5000 发现；独立 BLE 通知与配对引导 |
| 日夜主题、手机电量 | [main/main.c](../examples/managed-terminal/main/main.c) | [main/terminal_board.c](../main/terminal_board.c) / [main/ui/electric_ui.c](../main/ui/electric_ui.c) | 参考 UI 已接入，修改布局只改 UI 任务 |
| 车辆控制 | [vehicle.c](../examples/managed-terminal/snippets/vehicle.c) / [faults.c](../examples/managed-terminal/snippets/faults.c) | [main/terminal_board.c](../main/terminal_board.c) | 根工程为显式 DEMO；量产替换 CAN/UART |
| 车况与故障上报 | [vehicle.c](../examples/managed-terminal/snippets/vehicle.c) / [faults.c](../examples/managed-terminal/snippets/faults.c) | [main/terminal_board.c](../main/terminal_board.c) | 根工程为显式 DEMO；量产替换 CAN/UART |
| 感应解锁 | [接线说明](PROXIMITY_UNLOCK.md) | `main/terminal.c`、`main/terminal_control.h` | 独立 BLE 采样/校准、来源隔离回执；锁状态为 DEMO |
| 车辆开关机 | [接线说明](VEHICLE_POWER.md) | `main/terminal.c`、`main/terminal_control.h` | 主业务指令、实际状态回执；背光开关为 DEMO |
| 导航 HUD | [video.c](../examples/managed-terminal/snippets/video.c) | [main/hud_view.c](../main/hud_view.c) / [main/terminal_board.c](../main/terminal_board.c) | 转向、距离、道路等事件绘制，投屏期间也保留 |
| 导航视频、独立镜像 | [video.c](../examples/managed-terminal/snippets/video.c) | [main/terminal_video.c](../main/terminal_video.c) | 热点、UDP、H.264/MJPEG、显示回执；镜像不启动导航 |
| Android 通知 | [notifications.c](../examples/managed-terminal/snippets/notifications.c) | [main/notice_view.c](../main/notice_view.c) / [main/terminal_board.c](../main/terminal_board.c) | 核心通知快照、显示与移除；动作 API 需接 UI，需手机授权 |
| iPhone ANCS | [notifications.c](../examples/managed-terminal/snippets/notifications.c) | [main/ancs_adapter.c](../main/ancs_adapter.c) | 完整 GATT 订阅适配，需 iOS 授权；不能用意图代替就绪状态 |
| 通话与 HFP | [notifications.c](../examples/managed-terminal/snippets/notifications.c) | — | 编译示例；P4/C6 没有经典蓝牙音频，需外部 HFP 驱动 |
| 音乐按键 | [optional_features.c](../examples/managed-terminal/snippets/optional_features.c) | — | 编译示例；还需手机媒体会话与 UI 按键 |
| 路线与位置扩展 | [optional_features.c](../examples/managed-terminal/snippets/optional_features.c) | — | 按路线版本匹配的示例；依赖 APP 提供对应事件，不等于现有视频流 |
| 仪表风格切换 | [optional_features.c](../examples/managed-terminal/snippets/optional_features.c) | [main/terminal_board.c](../main/terminal_board.c) / [main/ui/electric_ui.c](../main/ui/electric_ui.c) | 两种基础参考布局，显示完成后持久化确认 |
| PNG 广告、一次播放 | [advertising.c](../examples/managed-terminal/snippets/advertising.c) / [ad_receipts.c](../examples/managed-terminal/snippets/ad_receipts.c) | — | 可编译 SHA-256 与 NVS 回执；需接真实 PNG 显示和安全空闲判定，根工程默认关闭 |
| Wi-Fi 大文件 | [bulk.c](../examples/managed-terminal/snippets/bulk.c) | — | 编译模板；SDK 负责 TLS，客户接 TCP；与视频按 purpose 分流 |
| OTA 传输 | [ota.c](../examples/managed-terminal/snippets/ota.c) | — | 显式模拟器：校验、续传、状态；不写 Flash、不重启、不升级 |
| 联调与排障 | [testing.c](../examples/managed-terminal/snippets/testing.c) | [tools/test_host.py](../tools/test_host.py) | 编译、内存/色彩/UI 算法检查；设备验收仍需实板 |
| 离线地图 | [接入说明](OFFLINE_MAPS.md) | `main/route_map_*.c` | 根工程已接压缩传输、有限窗口与 Wi-Fi 位置；需新版二进制 SDK |

## 建议接入顺序

1. 导入 SDK、编译、配置授权、配对，验证日夜主题。
2. 用真实确认结果替换 DEMO 车况/控车，测试旧会话与队列满重试。
3. 先 HUD 后视频，确认显示回执、停止与重连。
4. 驱动存在后再开启通知、外部 HFP、音乐或路线扩展。
5. 广告将 `advertising.h` 连接 `ad_receipts.h` 和 PNG 显示，消费回执在播放前提交。NVS 例子保存 64 条持久回执，满时失败；量产需与服务端约定保留策略，解绑不得清回执。
6. `ota.h` 仅供传输联调。量产还需签名清单校验、OTA 分区、持久化进度、非活动分区写入、启动校验和回滚。根工程 factory 分区不能直接用于量产 OTA。

继续阅读[设备配置](PROVISIONING.md)、[架构](ARCHITECTURE.md)和[验证](VALIDATION.md)。
