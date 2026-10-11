# Moto ESP32-P4 开源接入参考工程

[English](README.en.md) · [功能与代码入口](docs/FEATURES.md) · [架构](docs/ARCHITECTURE.md)

面向 JC1060P470C_I_W_Y / JD9165 1024×600 屏、ESP32-P4 + ESP32-C6 的完整应用参考。Wi-Fi 配对、导航/镜像接收、HUD、通知和仪表切换使用实际板级驱动；车辆数据始终标记 DEMO。

**本仓库开源应用与适配代码，Moto SDK 内部实现仍为闭源静态库。** 客户从接入团队取得 SDK 0.1.0 / ABI 8 / ESP-IDF 5.5.4 的二进制包和本设备授权。本仓库不包含 SDK 实现、二进制分发、设备授权或私钥。

## 首次接入准备

从[官网获取 APP](https://oou.net/zh/downloads)，在手机可联网时完成登录；安装渠道、后台环境或账号不清楚时联系[接入团队](https://oou.net/zh/contact)。由有权限的管理员确认本机已登记、型号开启 Wi-Fi 和本次测试功能。SDK ZIP、设备授权和可信公钥分开交付，见[材料清单](https://oou.net/zh/docs/platform-and-license)。

指定板型先走下面的根工程：它已包含无线与屏幕驱动。`examples/managed-terminal` 和 SDK 包内的 `examples/customer` 是自有板移植模板，直接编译烧录仍不能接通手机。首次连接按[设备配置](docs/PROVISIONING.md)完成授权写入、热点信息获取和 PIN 配对。

## 先运行完整 P4 工程

安装 ESP-IDF 5.5.4 及其工具链、Python 3.10+ 和 Git。SDK 包路径与 IDF 路径替换为本机实际值：

```sh
git clone https://github.com/oounet/moto-esp32p4-example.git
cd moto-esp32p4-example
python3 tools/import_sdk.py /path/to/moto-sdk-0.1.0-freertos-esp32p4-binary-20261011-abi8-proximity-docs4.zip
python3 tools/fetch_lvgl.py
. /path/to/esp-idf-v5.5.4/export.sh
idf.py build
```

SDK 导入会核验包内校验和，只安装头文件、静态库、组件和元数据到被 Git 忽略的 `vendor/moto-sdk`。LVGL 固定到官方 9.3.0 提交。组件管理器按 `dependencies.lock` 获取 Espressif 依赖，不需要任何相邻私有仓库。构建产物为 `build/moto_p4_example.bin`。

初次无授权构建可用于检查编译和屏幕驱动，设备显示 LICENSE REQUIRED，不会假装认证成功。取得可信公开验签头后重新构建：

```sh
idf.py -DMOTO_LICENSE_TRUST_HEADER=/absolute/path/to/moto-license-trust.h build
```

头文件声明 `static const uint8_t moto_license_trust_key[65]`，值为平台可信 P-256 公钥。不要从待验证的 License 中自行提取信任。授权写入步骤见 [设备配置](docs/PROVISIONING.md)。

仅在确认板型、接线、原分区和备份后，使用自己的串口执行 `idf.py -p PORT flash monitor`；已有设备且分区相同时可只更新 `app-flash`。这些命令会写硬件，本项目的构建和测试不会自动执行它们。

## 再按功能学习与移植

- `main/`：完整板端应用，使用 SDK **公开的底层 C API**，包含 Wi-Fi 主业务、独立 BLE 通知及视频任务。此处不是 SDK 内部源码。
- `examples/managed-terminal/`：面向新平台的简单 FreeRTOS ABI 8 接口，SDK 管理任务和事件；含各功能可编译示例及可替换 BSP。**独立工程，不和 main 创建两个终端实例。**
- `docs/FEATURES.md`：逐项列出代码入口、运行状态、需要客户实现的部分和验证方法。广告、OTA、通话示例不代表本开发板已具备对应播放器、启动分区或 HFP 硬件。

```sh
cd examples/managed-terminal
idf.py build
```

先实现 `main/board_driver.c` 的授权、Wi-Fi 主连接和显示，再参考 `snippets/` 添加功能。默认未实现驱动返回失败，可选功能关闭。不要把模板编译成功当作已经接入设备。

## 验证

```sh
python3 tools/test_host.py
python3 tools/audit_public.py
```

验证记录见 [VALIDATION.md](docs/VALIDATION.md)。本次公开版本的 UI 为原生 LVGL 参考界面，只使用可再分发字体；没有打包原仪表图片及系统字体。P4/C6 无经典蓝牙 HFP，通话音频需要外部硬件。SDK 队列、UI、解码、无线与掉电持久化必须在客户目标板独立验证。

应用代码采用 MIT，第三方代码/字体保留原许可，见 [THIRD_PARTY.md](THIRD_PARTY.md)。此许可不包含 Moto SDK 二进制及其头文件的再分发授权。

## 离线地图

根工程包含整包压缩接收、路线底图及 Wi-Fi 位置显示，见[离线地图接入](docs/OFFLINE_MAPS.md)。需 revision 14 或更新的匹配 SDK，旧包的结构与符号不兼容。升级时将原 vendor/moto-sdk 移到本地备份目录后重新导入；不要只替换头文件。

当前接入要求 revision 14 / ABI 8，网络监听、连接代次、背压和失效清理由 SDK 统一管理；同时保留已有 CRC32 与消息解析优化。实际吞吐和帧率仍需在目标板测量，见 [验证记录](docs/VALIDATION.md)。

## 资源、升级与售后

参考镜像和静态内存、运行期测量及自有 BSP 移植见 [资源与平台移植](https://oou.net/zh/docs/resources-and-porting)。ABI 升级按 [版本与迁移](https://oou.net/zh/docs/versions-and-migration) 成套替换头库。核心示例的 `license_auth` 记录为 67 字节，不是托管入口的 72 字节格式；分区与掉电要求见 [持久化](https://oou.net/zh/docs/persistence)。管理员批量授权与换板清绑见 [管理与交付](https://oou.net/zh/operations)。
