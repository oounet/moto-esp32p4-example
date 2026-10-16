# 精简矢量绘制核心 / lean vector renderer

2026-10-15：`main/route_vector_view.c/.h` 提供纯 RGB565 绘制，已接入固件构建和 `route_map_store`。需要完整导入 r25 / FreeRTOS ABI 8 的匹配头文件及静态库，端口就绪后才声明矢量能力。输入是经 SHA、身份和解析预算验证的不可变 RVP1 窗口及同一导航/路线的位置。

The RGB565 compositor is connected to the firmware build and map store. Import the complete matching r25 / FreeRTOS ABI 8 headers and archives. Capability is advertised only with a ready port. Inputs are immutable RVP1 windows validated for SHA, identities and parser budgets, with matching navigation/route positions.

绘制范围为纯色背景、按层级排序的道路线、规划路线和位置箭头。无堆分配、LVGL 对象、字体、地图图片或 JPEG 解码。支持 320–2048 × 200–2048 像素、北向上和日夜模式。返回值表示整个视口的块是否都已准备，已确认空块算覆盖、没有索引的块算缺失。覆盖不完整时仍画已有道路、有效路线和位置；调用者必须传播未就绪状态。

The compositor draws a solid background, ordered road lines, route and position arrow. It uses no heap allocations, LVGL objects, fonts, map images or JPEG decoding. Supported viewports are 320–2048 × 200–2048 pixels, north-up, with day/night colors. The return value reports complete viewport block coverage; indexed empty blocks count as covered, missing blocks do not. Partial coverage still draws available geometry and the valid position, so callers must propagate the incomplete status.

`route_vector_store` 以独立 worker 校验并原子发布快照，短锁与引用计数保护绘制和取消生命周期；SDK owner 轮询提交结果。`route_map_store` 管理 TTL、冻结最后真实位置。`fresh=false` 只让箭头变灰，不产生位置外推。视频优先，停流后本地绘制；覆盖不足仍显示有效路线/位置并传播未完整状态。

The vector store validates on a worker and atomically publishes immutable snapshots, protected by short locks/reference counts. The SDK owner polls completion; the map store owns TTL and the last actual position. A stale arrow becomes gray without extrapolation. Video takes priority; local drawing resumes when video stops. Partial coverage preserves valid route/position geometry and reports incomplete status.

参考车机仅接收 RVP1/RVZ1，单窗口上限 1 MiB；已移除地图 JPEG 缓存和图片地图回退。没有道路窗口时，仍以相同画线器显示当前认证路线及有效位置。全球经度块环绕，支持跨日期变更线；不绑定城市名单。视频 JPEG 解码保持独立。

The reference terminal accepts only RVP1/RVZ1 windows, up to 1 MiB. Map JPEG caches and raster-map fallback have been removed. Without a road window, the same compositor draws the authenticated route and actual position. Longitude blocks wrap across the date line, with no city whitelist. Video JPEG decoding remains independent.

`tests/test_route_vector_view.c` 覆盖桥梁描边遮挡、空块/缺块、身份拒绝、日夜、位置变灰、视口边界及超长线段裁剪。存储回归覆盖校验、取消和快照生命周期。先向接入团队取得与公开头文件匹配、为本机系统/架构编译的 SDK 静态库；ESP32-P4 静态库不能在电脑上运行。测试只编译本仓库应用代码并链接该二进制，不读取私有源码。

The renderer tests cover bridge casing, empty/missing blocks, identity rejection, themes, stale positions, viewport bounds and clipping. Store tests cover validation, cancellation and snapshot lifetime. Obtain a matching SDK archive compiled for the host OS/architecture from the integration team; the ESP32-P4 archive cannot run on a computer. Tests compile only this repository's application code and link the supplied binary.

```sh
MOTO_SDK_HOST_LIBRARY=/path/to/host/libmoto_sdk.a sh tools/test_vector_store.sh
```

默认读取 `vendor/moto-sdk/include`；如果主机 SDK 另附配套公开头文件，用 `MOTO_SDK_HOST_INCLUDE` 指定。缺少匹配二进制时脚本明确失败，不会去查找相邻私有工程。

Headers default to `vendor/moto-sdk/include`; set `MOTO_SDK_HOST_INCLUDE` if the host SDK ships a separate matching public include directory. Missing binaries fail explicitly without searching adjacent private projects.

宿主结果只验证数据和绘制算法；没有证明 P4 时序、LCD 显示、手机协商或锁屏行为。生产接入必须使用匹配的 SDK 头/库与包元数据，不能单独拷贝新头来配旧静态库。

Host results establish data and rendering behavior only, not P4 timing, LCD presentation, phone negotiation or lock-screen behavior. Production integration requires matching SDK headers, archives and package metadata; do not pair new headers with an old archive.
