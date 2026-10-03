# 设备配置 / Device provisioning

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

## 3. 配对与验收 / Pair and verify

APP 添加设备，输入屏幕 PIN；重连应不再重复配对。验证日夜主题、导航、通知，再按功能清单继续。USB 命令仅在 `CONFIG_MOTO_FACTORY_USB=y` 时启用；量产可关闭 USB 配置，改由受控工站预写 NVS，验签要求相同。

Add the device in the APP and enter its displayed PIN. Verify reconnection, theme, navigation and notifications. USB provisioning requires `CONFIG_MOTO_FACTORY_USB=y`; production may disable it and use controlled NVS provisioning, with the same verification requirement.
