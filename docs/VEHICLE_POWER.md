# 车辆开关机

[English](VEHICLE_POWER.en.md)

型号授权 `vehicle.power.control`（bit 12），收到并验证签名配置后生效。使用 ABI 8 配套头文件与静态库。

## 车辆电源

Wi-Fi 主实例接收 `MOTO_POWER_SET`（55）：`target=0`，`value=0` 关机、`1` 开机。`main/terminal_control.h` 显式映射命令到状态字段，禁止按消息编号相减推算。

本参考工程只演示背光开关，Wi-Fi、BLE、LCD 扫描和状态上报继续运行，可从 App 再次开机。没有真实车辆电源执行器。量产需替换队列执行逻辑，待控制器实际确认后再完成命令，并上报实际状态：续航仍为 field 11，电源为 field 12；读不到时标记 unknown。

## 接入入口与验收

根工程与 `examples/managed-terminal` 均支持主业务电源命令。执行前检查原会话、截止时间和安全联锁；完成回到原实例，结果投递重试不能重复执行物理动作。

实板检查真实开关机、拒绝、超时、断连和再次开机；正式车辆保留接收开机指令的待机电源及通信。构建与宿主检查见 [验证记录](VALIDATION.md)，感应锁另见 [感应解锁](PROXIMITY_UNLOCK.md)。
