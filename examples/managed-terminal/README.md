# 简单 FreeRTOS 接入 / Managed FreeRTOS integration

先按仓库 [README](../../README.md) 导入二进制 SDK，再在本目录运行 `idf.py build`。替换 `main/board_driver.c` 的授权读取、网络状态和 AP/IP 启动和 PIN/主题显示。默认驱动返回失败，可选功能关闭。只有一个 terminal 实例；不同时运行根工程。

Import the binary SDK following the [root README](../../README.en.md), then run `idf.py build` here. Implement license loading, network readiness and AP/IP setup and PIN/theme rendering in `main/board_driver.c`. Default stubs fail explicitly. Do not start the full-board terminal in this firmware.

先确认产品范围：本工程封装单条主业务会话。完整仪表所需的独立 BLE、感应解锁、底图使用根工程 Core 结构。镜像/音频可在 start 前调用 `moto_terminal_set_mirror` / `moto_terminal_set_audio`；镜像需 `config.network` 和真实视频能力，音频需真实 sink。本模板默认关闭这些功能。`moto_terminal_post` 只扩展当前实例已配置的功能，不自动挂接辅助实例。可保留的 BSP/UI/车辆驱动及切换步骤见 [迁移指南](https://oou.net/zh/docs/versions-and-migration)。

Confirm the product scope first: this project wraps one primary session. Independent BLE, proximity and basemaps use the root Core architecture. Configure optional mirror/audio before start with `moto_terminal_set_mirror` / `moto_terminal_set_audio`; mirroring requires `config.network` and real video capability, and audio requires a real sink. This template leaves them disabled. `moto_terminal_post` extends configured operations on the existing instance; it does not attach companions. See the [migration guide](https://oou.net/en/docs/versions-and-migration) for reusable BSP/UI/vehicle drivers and switching steps.

[逐项功能与文件](../../docs/FEATURES.md) · [Feature map](../../docs/FEATURES.en.md) · [官网 API / API guide](https://oou.net/en/docs/api-reference)

`main.c` 展示事件循环、业务分流和完成回执。`snippets/*.c` 都参与编译，按需调用，不会自行启用硬件功能。主连接由 `config.network` 托管；`bluetooth.c` 仅展示高级外部传输模式，两种模式互斥。视频事件的 session 和 peer token 不可混用。

`main.c` shows event dispatch and completion receipts. All snippets compile but need explicit wiring. Managed primary I/O uses config.network; bluetooth.c is an alternative external-transport example. Preserve original data on FULL in that mode; video session and peer token are distinct.

广告先用 Wi-Fi 主通道小 PNG：`advertising.h` 提供哈希与播放接口，`ad_receipts.h` 提供真实 NVS 去重。单一 SDK 任务调用；存储上下文活到终端销毁后。再接 `bulk.c` 启用独立 bulk 传输。OTA 的 `ota.h` 是显式模拟传输，不安装固件。HFP、PNG 显示、TCP、解码器等接口仍需实际驱动。

Start ads with a small PNG over the Wi-Fi business session. `advertising.h` provides hash/player hooks; `ad_receipts.h` implements durable NVS deduplication. One SDK task owns calls; keep context through destruction. Then wire `bulk.c` for independent bulk transfers. `ota.h` explicitly simulates transport without installing firmware. HFP, PNG display, TCP and decoding require actual drivers.

`notifications.c` 的核心快照示例使用 `moto_event`；托管 UI 示例使用 `moto_terminal_event`，两者不可混用。当前 App 的 Android 通知与感应解锁需要根工程中的独立 BLE 辅助实例，单独开启 Wi-Fi 实例的 notifications 不会建立它。

The core snapshot example in `notifications.c` uses `moto_event`; managed UI examples use `moto_terminal_event`. Do not mix them. Current APP notifications and proximity need the full application's separate BLE companion; enabling notifications on a Wi-Fi instance does not create that companion.
