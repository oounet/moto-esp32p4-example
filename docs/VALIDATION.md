# 验证记录 / Validation

当前接入使用 SDK 0.1.0、package revision 14、FreeRTOS ABI 8；头文件与静态库必须来自同一个交付包。下方按日期保留历史构建证据，旧包不是当前接入要求。

Current integration requires SDK 0.1.0, package revision 14 and FreeRTOS ABI 8, with matching headers and archives. Dated entries below preserve historical evidence, not current package requirements.

## ABI 8、感应锁与电源 / ABI 8, proximity and power — 2026-10-11

根工程和 managed-terminal 使用 revision 14 / ABI 8 二进制包完成 ESP-IDF 5.5.4 构建。最终包的公开头文件和两个静态库与已构建版本逐字节一致；导入校验、4 项导入安全测试和宿主 ASan/UBSan 通过，新增控制字段映射检查。根工程同步了 BLE 校准/RSSI、来源隔离控制回执、DEMO 背光电源和公开通知快照接口。

The root and managed-terminal projects build with revision 14 / ABI 8 on ESP-IDF 5.5.4. Final-package headers and both archives match the completed builds byte-for-byte. Import verification, four importer safety tests and host ASan/UBSan checks pass, including explicit control-field mapping. The root integrates BLE calibration/RSSI, origin-bound completions, DEMO backlight power and owned notification snapshots.

SDK 包 SHA-256 / package SHA-256: `c720437aac9a117e0b757617783662a2fdab3cbf68297d5ac7f0990835c1bc4c`。

本次公开 UI 未刷入设备；内部固件的安装记录不能代替此参考 UI 的实机验收。真实锁/电源执行器、感应距离、后台重连和 NVS 掉电恢复需独立验证。

The public UI was not flashed in this update; installation of internal firmware does not validate this reference UI. Real lock/power actuators, ranging, background recovery and NVS power-loss behavior require separate acceptance.

## 通用网络组件 / Portable networking — 2026-10-10

revision 13 / ABI 7 的完整根工程和 managed-terminal 已使用同包头文件、静态库完成 ESP-IDF 5.5.4 构建；最终包与两工程所链接的接口/库逐字节一致。公开宿主 ASan/UBSan 与导入测试通过。SDK 内部另验证非 Socket 核心构建、网络背压/代次、托管真实 TLS 短写与同 IP 重连，不公开内部测试源码。参考 UI 没有刷入设备；本轮构建不证明首次入网时延或长时间镜像。

Root and managed-terminal build with matching revision 13 / ABI 7 headers and archives. Final-package interfaces/libraries match the completed builds byte-for-byte. Public host sanitizer/import tests pass. Internal SDK checks also cover core-only builds without sockets, backpressure/generations, real TLS short writes and same-IP recovery. The public UI was not flashed; first-join timing and sustained mirroring still require target acceptance.

## Wi-Fi 断连清理 / Wi-Fi disconnect cleanup — 2026-10-10

根工程在单客户端 AP 离线或停止后，通过代次在 SDK 所有者队列关闭旧连接，再接受重新认证。根工程与 managed 示例编译链接通过，现有 ASan/UBSan 主机测试通过；这些测试不模拟无线事件时序。公开参考 UI 未刷入设备，目标板仍须检查断开图标、快速重连，以及普通主 TCP 断开时镜像扩展的续期。

The root application uses an AP-loss generation to close stale connections on the SDK owner queue before accepting fresh authentication. Root and managed builds and existing sanitizer host tests pass; the host tests do not simulate radio event timing. The public reference UI was not flashed. Verify disconnected icons, rapid reconnects and delegated mirror renewal after ordinary primary TCP loss on the target board.

## SDK 校验优化 / SDK validation optimization — 2026-10-10

匹配 revision 12 的头文件与两个静态库已经完成根工程、managed-terminal 的 ESP-IDF 5.5.4 编译链接；主机测试和公开索引审计通过。新库优化 CRC32 与消息外层解析，ABI 6 和线协议不变。本轮没有刷板或测量开发板运行速度，函数基准不能代替真实网络、解码和显示验收。

The root and managed projects build and link with matching revision 12 headers and archives on ESP-IDF 5.5.4. Host tests and the public-index audit pass. CRC32 and envelope parsing are optimized with no ABI 6 or wire-format change. No board flashing or runtime timing was performed; function benchmarks do not establish network, decoder or display performance.

## 历史构建 / Historical build — 2026-10-04

2026-10-04：ESP-IDF 5.5.4、ESP32-P4、SDK 0.1.0 ABI 3 二进制包。包 SHA-256：`2a7a91d9ab54a03b31cddd8d3be2f01dc371c5bb956476d06febdd4dc2f5da18`。LVGL 固定提交见 `tools/fetch_lvgl.py`。

## 自动检查 / Automated checks

- 根工程 `idf.py build` 成功：完整 P4 应用链接交付静态库，没有读取 SDK 实现。
- `examples/managed-terminal` 的 `idf.py build` 成功，包含各功能片段。BSP 空缺仍需客户实现。
- `python3 tools/test_host.py`：H.264 SIMD 分配、PPA 色彩排列、仪表采样、cockpit 裁切、HUD 边界；ASan/UBSan 检查。
- NVS 回执主机模拟：首次接收、相同/不同哈希、消费去重、重开、commit 失败、容量满及损坏拒绝。模拟测试不等于真实 NVS 掉电测试。
- SDK 导入器：路径穿越、校验和损坏、已有目录保护。
- 公开 Git 索引检查：不含 SDK 实现、库/固件/构建产物、License、私钥及本机绝对路径。

Both ESP-IDF projects build against delivered archives. Host tests exercise allocator, color packing, view geometry, mocked receipt durability/failure and package import rejection. These establish compilation and tested logic only.

## 目标板验收 / Board acceptance

本次没有刷机、变更已有设备配置或重新验证无线/显示。开源 UI 为新的原生 LVGL 参考布局，须在目标板实际检查排版和帧率。以下项目由客户在自己的板子上执行：

No hardware was flashed or reconfigured for this publication. The new native LVGL reference layout needs visual/frame-rate checks on the target board. Validate:

1. 本机 License、错误公钥/错误硬件标识拒绝、PIN 与重连。Correct device license, invalid-trust rejection, PIN and reconnect.
2. Wi-Fi 主业务断连、同账号接管、旧会话不能控车；蓝牙单独断开不影响业务。Wi-Fi disconnect, same-owner handoff and stale-session rejection; independent Bluetooth loss must not stop business traffic.
3. 主题、Android 通知权限、iOS ANCS 授权/撤销。Theme, Android notifications and iOS ANCS permission/revocation.
4. HUD 与视频并存；镜像不打开导航；Wi-Fi 切换、视频停止/恢复、显示回执。HUD/video coexistence, standalone mirror, Wi-Fi changes and actual presentation receipts.
5. 真实 CAN/UART 执行失败、超时与 FULL 重试，不能重复执行动作。Real control failure/timeouts and retry without duplicate actuation.
6. 广告在行驶中不播放、重传去重、消费提交前后掉电、满盘/损坏失败。Ads: safe idle, duplicate transfer, power cuts around consumption, full/corrupt storage.
7. 外部 HFP、手机音乐会话及路线扩展分别验证；未实现能力不声明。Validate external HFP/media/routes independently; advertise only implemented features.
8. OTA 模拟器只验证协议；生产升级需独立验收分区、签名、断电续写、安装后启动及回滚。Simulator only checks transport; production requires separate install/rollback acceptance.

`tests/test_config.c`、`test_notice_view.c`、`test_media_session.c` 等保留为公开应用回归素材；需要匹配主机平台的 SDK 库或板端测试适配，不属于默认主机测试通过声明。不要把 P4 归档链接到 x86/ARM 主机测试。

Additional public board regression sources may require matching host SDK archives or target adaptation; they are not included in the default passing host suite. P4 archives cannot link into a host executable.

## 离线地图更新 / Offline maps — 2026-10-08

使用 revision 8 / 20261008 二进制 SDK（头文件与库同包）：根工程和 managed-terminal 均已完成 ESP-IDF 5.5.4 编译链接。公开工程仅使用 vendor 中的交付接口，不依赖相邻 SDK 源码。主机测试新增实际 TCP 适配器的分片、短写、背压、限额、断开与认证失效检查；原有 HUD、仪表、颜色、内存与导入检查仍通过。完整地图核心和真实 TLS 互操作由 SDK/App 内部回归覆盖，不复制到公开工程。

Both projects build against revision 8 binary headers and archives. Host checks now include the production map TCP adapter's fragmentation, short writes, backpressure, bounded work, disconnect and revocation. Existing host checks pass. Core map/TLS interoperability tests remain in their owning repositories.

本次未将公开参考 UI 刷入设备；iPhone/P4 整包传输实测来自内部完整仪表工程。公开 UI、持续骑行、后台窗口更新和 Android 真机需单独验收。

The public reference UI was not flashed for this publication. Prior iPhone/P4 transfer evidence comes from the internal instrument application. Public UI, sustained riding, background window updates and Android hardware still require separate acceptance.

## Wi-Fi 主业务 / Wi-Fi business transport

2026-10-09：使用 SDK 0.1.0 / FreeRTOS ABI 5、ESP-IDF 5.5.4 匹配二进制包，根工程与 `examples/managed-terminal` 编译链接通过；主机 ASan/UBSan、Python 检查与公开文件审计通过。最终包的头文件、静态库和组件文件逐字节匹配已构建的导入内容。

The root and managed projects build against matching ABI 5 archives. Host sanitizer tests, Python checks and the public distribution audit pass. Build and host evidence do not establish phone Wi-Fi/Bluetooth interoperability or physical UI acceptance.
