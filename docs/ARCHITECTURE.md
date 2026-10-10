# Architecture / 架构

## 完整 P4 应用 / Full board application

`app_main` 初始化 LCD 和 LVGL 所有者任务，再运行 `p4_terminal_start`。`terminal.c` 由 NimBLE host 串行调用 SDK 的公开 `moto_sdk/moto_tls` 接口；TCP 主实例负责业务，BLE 候选只认证并接管通知辅助连接。SDK 的实现来自 `libmoto_sdk.a`，不在应用目录中。

`terminal_board.c` 复制回调数据到有界状态/队列。LVGL 所有者任务独占所有 UI 调用；车辆任务的 DEMO 结果由 host 回报。`terminal_video.c` 独占重组、解码和视频代次；UDP 来源必须对应热点关联手机。LCD 实际显示后才回报帧呈现。ANCS 的完整 NimBLE 适配位于 `ancs_adapter.c`，解析实现来自 SDK 库。

The full application serializes public core/TLS APIs on the NimBLE host. UI and decoder tasks own their resources and receive copied values. The TCP instance owns business traffic; BLE candidates authenticate only for notification companion handoff. Decoder completion is distinct from LCD presentation. Do not call core APIs concurrently or retain callback pointers.

## 简单标准入口 / Managed interface

`examples/managed-terminal` 独立使用 `moto_freertos/terminal.h`，由闭源库管理 SDK 任务、认证、事件复制和 bulk TLS。客户只实现 board_driver 的端口。它用于从零移植到新板，不在 P4 主工程中再次启动；主工程展示 Wi-Fi 与 BLE 独立实例及通知连接接管。

The separate managed example owns one terminal through the ABI 6 facade. It is the recommended starting point for new BSPs. It is not a second running terminal inside the full board firmware. Feature snippets are compiled as part of this project and illustrate public API contracts without copying SDK implementation.

## 生命周期 / Lifetime

端口上下文存活到销毁结束。事件按值复制，保存原 connection/session/request；执行前再核对过期和会话。只重试未被 SDK 接收的提交，不能重复执行车辆操作。退出时先停止输入和调用者，stop 返回 AGAIN 时保留句柄，成功后 destroy。底层 send/close 回调必须能完成 SDK 清理。

Keep contexts alive through destruction; copy events and preserve original session identifiers. Retry only unaccepted submissions, never repeat physical actions. Quiesce callers before stop/destroy, retaining the instance while stop returns AGAIN.

## SDK 与发布边界 / Distribution

`vendor/moto-sdk` 由导入脚本建立，始终被 Git 忽略。公开目录只包含应用、板级适配、示例、测试和许可证。字体为 Noto CJK 的 OFL 派生位图，LVGL 自带字体保持上游许可；未复制原项目系统字体或来源图片。

Only application, BSP, examples, tests and notices are public. Proprietary SDK headers/archives stay in ignored vendor storage. Never copy SDK src/ports or device-specific provisioning data into the repository.

## 地图数据 / Map data

根工程由 SDK owner 线程接收认证 TCP 地图包；完整校验后，在互斥量内替换显示数据。显示任务持锁读取，回调复制 Wi-Fi 路线与位置；导航结束清理内存。地图不包含 SDK 私有实现。

The SDK owner receives authenticated map transfers. Validated maps replace display state under a mutex; Wi-Fi route and position events are copied. Navigation end clears temporary memory. See [integration](OFFLINE_MAPS.en.md).

## 无线通道 / Wireless channels

AP 在启动时提供 DHCP，UDP 5000 用于发现、TCP 5004 承载主业务 TLS。BLE 配对引导与 Android 通知 / iOS ANCS 使用独立辅助 SDK/TLS 实例；两个实例在同一 owner 线程串行执行，共享持久化 owner 快照。蓝牙断开不终止主导航，业务断开不主动清理通知会话。UI 的 Wi-Fi / 蓝牙图标分别读取真实认证状态。两枚图标使用相同的 24 px 几何尺寸、2 px 线宽与状态色；未连接显示灰色斜线。公开参考仪表不内置无线图标，由 framebuffer 叠加层统一绘制一组；自定义仪表若自行绘制，仅在全屏视频/HUD 页面启用叠加层，首页小地图预览不重复叠加。

The AP starts at boot. UDP 5000 provides discovery; TCP 5004 carries authenticated business traffic. Optional BLE bootstrap and Android notifications / iOS ANCS use a separate SDK/TLS instance. One owner thread serializes both instances and shared durable owner records. Each radio icon reflects its own authenticated connection. Both use 24 px geometry, a 2 px stroke and the same state colors; disconnected radios are gray with a slash. The public reference instrument has no embedded radio glyphs, so the framebuffer draws one pair. If a custom instrument draws its own pair, enable the overlay only for fullscreen video/HUD, not for an inset home preview. Video or bulk cleanup preserves the AP and primary listener.

## iOS 镜像续期 / iOS mirror lease

完整 P4 应用在 TCP 5005 提供独立镜像 TLS 实例，配置 `mirror_auxiliary=true` 并通过 `moto_connect_mirror` 绑定主实例。广播扩展使用主连接授予的本流随机授权续期，只能保活或停止该流；主连接关闭或流切换后授权失效。该端点与主业务在同一 owner 线程串行执行，辅助 CLOSED 不清理主显示/热点。JPEG 引擎和输出缓冲在启动时分配并保留到重启，视频 worker 独占使用。

The full P4 app serves a separate TLS mirror lease on TCP 5005 using `mirror_auxiliary=true` and `moto_connect_mirror`. The capture extension may renew or stop only its granted stream. Closing the primary connection or replacing the stream revokes the grant. Both instances run on the same owner thread; auxiliary closure must not tear down the primary display or hotspot. One JPEG engine/output buffer is reserved at boot and reused exclusively by the video worker.

TLS 动态缓冲使用 PSRAM（`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`），为 SDIO/JPEG 保留内部 DMA 内存。TCP 断开日志记录链路、错误码和发送失败时的内部空闲量，不记录凭据。

TLS dynamic buffers use PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`) to leave internal DMA memory for SDIO/JPEG. TCP diagnostics identify the channel, error code and internal memory on send failure without recording credentials.

主 TCP 意外丢失时，只有已委派的镜像可调用 `moto_detach_mirror` 保留；无主 socket 时仍在 owner 线程 poll 主实例。期间拒绝新主连接，辅助停止、20秒无续期或撤权后释放。显式用户断开仍使用 `moto_disconnect`。

On unexpected primary TCP loss, only an already delegated mirror can use `moto_detach_mirror`. Continue polling the primary on its owner thread without a socket. Reject replacement primary connections until extension stop, twenty seconds without renewal, or revocation releases the session. Explicit user disconnection still uses `moto_disconnect`.
