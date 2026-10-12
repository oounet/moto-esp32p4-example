座桶控制用于打开电动车、摩托车的坐垫盖，执行一次开锁动作，不提供关闭指令或虚构的盖板状态。

## 开启能力

后台设备型号勾选 `vehicle.seat.control`（座桶），对应 bit 13、`MOTO_SEAT_CONTROL`、掩码 `8192`。新增目录项不会自动给已有型号授权。使用 SDK revision 16 / FreeRTOS ABI 8 的配套头文件和静态库，以及支持此能力的 APP。

APP 在首页“更多”显示“打开座桶”，只操作当前车辆；需要已认证的主业务连接。此操作没有状态字段，不依赖电量等遥测是否已经到达，也不走 BLE 通知辅助连接。

## 执行开盖

收到 `MOTO_EVENT_VEHICLE_COMMAND`，显式检查 `e.message == MOTO_SEAT_OPEN`（59）。`e.target` 固定为 `0`，`e.value` 固定为 `1`；其他值被 SDK 拒绝。线协议 body 为 `{0:0,1:1,2:会话截止时间}`，结果沿用 `CONTROL_RESULT`（54）。

将完整事件复制到车辆驱动队列。执行前核对原连接、会话、截止时间及车辆控制器要求的安全条件。接上厂商提供的 CAN/UART 指令或实际锁执行器；SDK 不规定 GPIO、电平或开锁脉冲时长。

控制器确认执行后，托管入口使用原事件回报：

```c
moto_terminal_complete(terminal, &saved_event, MOTO_SUCCEEDED, 0);
```

核心入口使用原主业务实例的 `moto_complete_command`，保留原 `session_id` 和 `request_id`。明确失败报 `MOTO_FAILED`，已经发送但无法确认结果报 `MOTO_UNKNOWN`。`ACCEPTED` 仅表示接收，不能提示开盖成功；结果队列满只重试结果，不重复驱动物理动作。

## 状态与验证

`controls[4]` 和 96 字节签名配置结构保持不变；能力共 14 位，状态仍为 13 项（field 0–12）。禁止向 `state.values[13]` 写入数据，不把控制器执行确认当成盖板位置传感器读数。

P4 参考工程把座桶标记为 DEMO，仅显示演示执行结果，不驱动任何座桶硬件。量产需替换驱动，验证未授权、离线、连续点击、拒绝、超时、旧会话迟到回执及真实开盖；断线后不自动重发开盖指令。
