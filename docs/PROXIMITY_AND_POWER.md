# 感应解锁与车辆电源

[English](PROXIMITY_AND_POWER.en.md)

根工程使用 ABI 8 的核心接口。后台型号分别开放 `vehicle.proximity.unlock`（bit 11）和 `vehicle.power.control`（bit 12），App 下发签名配置后生效。本地支持位不能授予云端权限。

## 感应解锁

`main/terminal.c` 在独立 BLE 辅助实例注册 `moto_proximity_port`，每 500 ms 从对应 NimBLE 连接读取 RSSI，交给 `moto_proximity_sample`。采样失败传入无效值以中断当前靠近判定。算法、校准消息和超时由 SDK 实现，应用不再自行估算距离。

用户在 App 发起约两米处的稳定采样，车端确认保存后才算开启。`proximity_load/save` 将校准记录保存在 NVS，以 owner 和 App 安装标识的哈希隔离；读取或提交失败如实返回错误。RSSI 受人体遮挡、朝向和环境影响，不能视为精确两米测距。实板需要检查走近、走远、停留、断连和手动锁车后的行为。

SDK 本地事件 `MOTO_PROXIMITY_UNLOCK_TRIGGER`（56）进入控制队列；它不是手机可发送的解锁指令。队列保存来源槽、连接代次、会话和截止时间，完成时回到原 SDK 实例。断开 Wi-Fi 不取消 BLE 来源的动作；断开对应 BLE 后旧工作不能进入新会话。手动锁车成功后调用 `moto_proximity_reset`，要求先离开再靠近。

## 车辆电源

Wi-Fi 主实例接收 `MOTO_POWER_SET`（55）：`target=0`，`value=0` 关机、`1` 开机。`main/terminal_control.h` 显式映射命令到状态字段，禁止按消息编号相减推算。

本参考工程只演示背光开关，Wi-Fi、BLE、LCD 扫描和状态上报继续运行，可从 App 再次开机。没有真实车辆电源或锁执行器。量产需替换队列执行逻辑，待控制器实际确认后再完成命令，并上报实际状态：锁为 field 3，续航仍为 field 11，电源为 field 12；读不到时标记 unknown。

## 接入入口与验收

`examples/managed-terminal` 支持主业务电源命令，独立 BLE 感应锁按根工程的核心接口结构接线。不要把 BLE 事件交给 Wi-Fi terminal 完成接口。通知也使用该独立 BLE 路径，在回调内通过 `moto_event_get_notification` 取得完整副本。

构建与宿主检查见 [验证记录](VALIDATION.md)。硬件验收需分别检查校准持久化、旧会话隔离、真实执行失败、超时、重复操作及手机后台断连恢复；编译成功不代表车辆已解锁或断电。
