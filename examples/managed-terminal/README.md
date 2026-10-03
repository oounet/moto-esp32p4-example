# 简单 FreeRTOS 接入 / Managed FreeRTOS integration

先按仓库 [README](../../README.md) 导入二进制 SDK，再在本目录运行 `idf.py build`。替换 `main/board_driver.c` 的授权读取、BLE 发送/关闭/启动和 PIN/主题显示。默认驱动返回失败，可选功能关闭。只有一个 terminal 实例；不同时运行根工程。

Import the binary SDK following the [root README](../../README.en.md), then run `idf.py build` here. Implement license loading, BLE transport/advertising and PIN/theme rendering in `main/board_driver.c`. Default stubs fail explicitly. Do not start the full-board terminal in this firmware.

[逐项功能与文件](../../docs/FEATURES.md) · [Feature map](../../docs/FEATURES.en.md) · [官网 API / API guide](https://oou.net/en/docs/api-reference)

`main.c` 展示事件循环、业务分流和完成回执。`snippets/*.c` 都参与编译，按需调用，不会自行启用硬件功能。BLE 广播/GATT/收包见 `bluetooth.c`；FULL 时保留原数据重试。视频事件的 session 和 peer token 不可混用。

`main.c` shows event dispatch and completion receipts. All snippets compile but need explicit wiring. Preserve original data on FULL; video session and peer token are distinct.

广告先用 BLE 小 PNG：`advertising.h` 提供哈希与播放接口，`ad_receipts.h` 提供真实 NVS 去重。单一 SDK 任务调用；存储上下文活到终端销毁后。再接 `bulk.c` 启用 Wi-Fi。OTA 的 `ota.h` 是显式模拟传输，不安装固件。HFP、PNG 显示、TCP、解码器等接口仍需实际驱动。

Start ads with a small PNG over BLE. `advertising.h` provides hash/player hooks; `ad_receipts.h` implements durable NVS deduplication. One SDK task owns calls; keep context through destruction. Then wire `bulk.c` for Wi-Fi. `ota.h` explicitly simulates transport without installing firmware. HFP, PNG display, TCP and decoding require actual drivers.
