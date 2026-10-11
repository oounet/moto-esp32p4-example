# 设备配置 / Device provisioning

本工程默认开启 USB 配置。若曾改过配置，首次构建前运行 `idf.py menuconfig`，在 **Moto production provisioning → Enable optional USB License provisioning endpoint** 勾选启用，保存后重新构建；对应配置为 `CONFIG_MOTO_FACTORY_USB=y`。下面的硬件标识、授权和热点命令都依赖它。

USB provisioning is enabled by default. If configuration was changed, run `idf.py menuconfig`, enable **Moto production provisioning → Enable optional USB License provisioning endpoint**, save and rebuild (`CONFIG_MOTO_FACTORY_USB=y`). All identity, license and hotspot commands below require it.

## 1. 编译与读取硬件标识 / Build and read identity

无授权时也能编译，但设备拒绝认证。刷写仅适用于 README 指定板型；确认分区与备份后手动执行。关闭串口 monitor，再运行只读命令：

A license-free build rejects authentication. Flash only the specified board after checking partitions/backups. Stop the serial monitor, then read identity:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/provision.py --port PORT
```

把返回的 `hardwareId` 提供给签发团队，取得本机 512 字节 `.motolic` 与可信公钥头。工具不生成授权、不输出授权内容；不能拿其他设备授权代用。

Send `hardwareId` to the provisioning team for a device-specific 512-byte `.motolic` and trusted public-key header. The tool neither signs nor prints licenses. Another device's license is not valid.

## 2. 配置验签信任 / Configure trust

公钥头声明 `static const uint8_t moto_license_trust_key[65]`，为团队提供的完整 P-256 公钥。使用独立受控文件，不从授权文件自建信任，不将签名私钥放入固件或仓库。

The trusted header declares `static const uint8_t moto_license_trust_key[65]`, containing the supplied P-256 public key. Keep signing private keys outside firmware and this repository.

```sh
idf.py -DMOTO_LICENSE_TRUST_HEADER=/absolute/path/to/moto-license-trust.h build
# After checking the existing partition layout / 核对已有分区后
idf.py -p PORT app-flash
python3 tools/provision.py --port PORT --license /absolute/path/to/device.motolic
```

收到 `MOTO_LICENSE_INSTALLED` 后设备重启。写入前验证签名及硬件绑定；已配置不同授权时拒绝覆盖，不自动擦除 NVS。配置失败先检查公钥、hardwareId 和签发包。

`MOTO_LICENSE_INSTALLED` is followed by reboot. Firmware verifies signature and hardware binding before committing. A different existing license is not overwritten and NVS is never automatically erased.

## 3. 获取热点信息 / Obtain hotspot details

本工程使用本机生成并保存的 WPA2 热点凭据，没有通用默认密码。首次通过 Wi-Fi 连接时，在本地 USB 串口获取。先关闭 `idf.py monitor` 与其他串口工具，再打开终端（`PORT` 换成自己的 USB 串口）：

The board generates and stores its own WPA2 credentials; there is no universal default password. For the first Wi-Fi connection, retrieve them through local USB. Close other serial tools first, then open your USB port:

```sh
python3 -m serial.tools.miniterm PORT 115200 --eol LF --dtr 0 --rts 0
```

输入下面一行并回车，行尾必须为 LF：

Send this JSON line followed by LF:

```json
{"command":"wifi_start"}
```

等待 `MOTO_MANUAL_WIFI` 回复，从本地输出读取 `ssid`、`password` 和 `ip`。正常 IP 为 `192.168.4.1`。此命令启动或复用热点，不重置授权或绑定；`MOTO_MANUAL_WIFI_NOT_READY` 表示任务未就绪或队列暂满，稍后重试；`MOTO_MANUAL_WIFI_FAILED` 时检查无线初始化。按 `Ctrl+]` 退出终端。回复包含热点密码，仅用于本地配网，不保存到普通日志或提交到仓库。USB 配置须启用 `CONFIG_MOTO_FACTORY_USB=y`。

Wait for `MOTO_MANUAL_WIFI` and read its `ssid`, `password` and `ip` locally (normally `192.168.4.1`). This starts or reuses the AP without resetting the license or pairing. Retry `MOTO_MANUAL_WIFI_NOT_READY` after tasks/queue space are ready; check radio initialization on `MOTO_MANUAL_WIFI_FAILED`. Exit with `Ctrl+]`. The reply includes the hotspot password; keep it out of routine logs and Git. USB provisioning requires `CONFIG_MOTO_FACTORY_USB=y`.

## 4. 配对与验收 / Pair and verify

1. 从[官网](https://oou.net/zh/downloads)获取 APP，联网登录；确认本机登记与型号配置和 APP 使用同一环境。热点可能没有互联网，首次云端认证仍需手机访问服务端。
2. 手机系统 Wi-Fi 设置加入上一步的 SSID，输入其密码。返回 APP 并允许本地网络访问。
3. 在 APP 添加设备并输入屏幕四位 PIN，包括开头的 `0`。Wi-Fi 路径无需先连接蓝牙。
4. 切换 APP 日夜外观，确认屏幕跟随；重启板子后再次连接，应不重复配对。
5. 再按[功能清单](FEATURES.md)分别验证导航与通知；通知需要额外完成 BLE 连接与手机系统授权。

Get the APP and sign in online; device registration/model settings must match the APP environment. Join the SSID/password above in the phone’s Wi-Fi settings, allow local-network access, then add the device and enter its four-digit PIN (including leading zeros). Initial cloud authentication still needs internet access. Verify theme changes and reconnection after a board reboot. This Wi-Fi path does not require Bluetooth; notifications additionally require BLE and system permission. Continue with the [feature checklist](FEATURES.en.md).

量产可关闭 USB 配置，改由受控工站预写 NVS，验签要求相同；产品需提供设备界面或受控配网方式让用户取得热点信息。

Production may disable USB provisioning and use controlled NVS provisioning with the same signature checks. Provide a device UI or controlled setup flow for users to obtain hotspot details.
