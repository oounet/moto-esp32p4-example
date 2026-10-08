# 验证记录 / Validation

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
2. 蓝牙断连、同账号接管、旧会话不能控车。Disconnect, same-owner handoff, stale-session rejection.
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
