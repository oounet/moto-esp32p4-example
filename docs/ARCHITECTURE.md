# Architecture / 架构

## 完整 P4 应用 / Full board application

`app_main` 初始化 LCD 和 LVGL 所有者任务，再运行 `p4_terminal_start`。`terminal.c` 由 NimBLE host 串行调用 SDK 的公开 `moto_sdk/moto_tls` 接口；候选手机只认证，接管成功后才成为业务所有者。SDK 的实现来自 `libmoto_sdk.a`，不在应用目录中。

`terminal_board.c` 复制回调数据到有界状态/队列。LVGL 所有者任务独占所有 UI 调用；车辆任务的 DEMO 结果由 host 回报。`terminal_video.c` 独占重组、解码和视频代次；UDP 来源必须对应热点关联手机。LCD 实际显示后才回报帧呈现。ANCS 的完整 NimBLE 适配位于 `ancs_adapter.c`，解析实现来自 SDK 库。

The full application serializes public core/TLS APIs on the NimBLE host. UI and decoder tasks own their resources and receive copied values. Candidate phones cannot operate the UI or vehicle before handoff. Decoder completion is distinct from LCD presentation. Do not call core APIs concurrently or retain callback pointers.

## 简单标准入口 / Managed interface

`examples/managed-terminal` 独立使用 `moto_freertos/terminal.h`，由闭源库管理 SDK 任务、认证、事件复制和 bulk TLS。客户只实现 board_driver 的端口。它用于从零移植到新板，不在 P4 主工程中再次启动；主工程现有双连接接管属于更高级的适配。

The separate managed example owns one terminal through the ABI 3 facade. It is the recommended starting point for new BSPs. It is not a second running terminal inside the full board firmware. Feature snippets are compiled as part of this project and illustrate public API contracts without copying SDK implementation.

## 生命周期 / Lifetime

端口上下文存活到销毁结束。事件按值复制，保存原 connection/session/request；执行前再核对过期和会话。只重试未被 SDK 接收的提交，不能重复执行车辆操作。退出时先停止输入和调用者，stop 返回 AGAIN 时保留句柄，成功后 destroy。底层 send/close 回调必须能完成 SDK 清理。

Keep contexts alive through destruction; copy events and preserve original session identifiers. Retry only unaccepted submissions, never repeat physical actions. Quiesce callers before stop/destroy, retaining the instance while stop returns AGAIN.

## SDK 与发布边界 / Distribution

`vendor/moto-sdk` 由导入脚本建立，始终被 Git 忽略。公开目录只包含应用、板级适配、示例、测试和许可证。字体为 Noto CJK 的 OFL 派生位图，LVGL 自带字体保持上游许可；未复制原项目系统字体或来源图片。

Only application, BSP, examples, tests and notices are public. Proprietary SDK headers/archives stay in ignored vendor storage. Never copy SDK src/ports or device-specific provisioning data into the repository.

## 地图数据 / Map data

根工程由 SDK owner 线程接收认证 TCP 地图包；完整校验后，在互斥量内替换显示数据。显示任务持锁读取，回调复制 BLE 路线与位置；导航结束清理内存。地图不包含 SDK 私有实现。

The SDK owner receives authenticated map transfers. Validated maps replace display state under a mutex; BLE route and position events are copied. Navigation end clears temporary memory. See [integration](OFFLINE_MAPS.en.md).
