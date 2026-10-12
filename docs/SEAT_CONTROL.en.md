Seat control releases the lid beneath an electric scooter or motorcycle seat once. It has no close command or synthesized lid-position state.

## Enable the capability

Enable `vehicle.seat.control` for the device model: bit 13, `MOTO_SEAT_CONTROL`, mask `8192`. Adding the catalog entry does not grant it to existing models. Use matching SDK revision 16 / FreeRTOS ABI 8 headers and archives and an APP that supports this capability.

The APP shows **Open seat** under **More** for the current vehicle. An authenticated primary business connection is required. The action has no telemetry field and does not wait for unrelated battery samples. It does not use the BLE notification companion.

## Release the seat

Handle `MOTO_EVENT_VEHICLE_COMMAND` by explicitly matching `e.message == MOTO_SEAT_OPEN` (59). `e.target` must be `0` and `e.value` must be `1`; the SDK rejects other values. The wire body is `{0:0,1:1,2:sessionDeadlineMs}`, with `CONTROL_RESULT` (54) replies.

Copy the complete event into the actuator queue. Before execution, check the original connection, session, deadline and controller interlocks. Implement the manufacturer's CAN/UART operation or physical lock driver; the SDK specifies no GPIO, voltage or pulse duration.

After the controller confirms execution, complete the original managed event:

```c
moto_terminal_complete(terminal, &saved_event, MOTO_SUCCEEDED, 0);
```

Core integrations use `moto_complete_command` on the original primary instance with the original session and request IDs. Report `MOTO_FAILED` for explicit rejection and `MOTO_UNKNOWN` when a sent action cannot be confirmed. `ACCEPTED` means received, not executed. If the result queue is full, retry only the result, never the physical release.

## State and validation

The four control-limit entries and 96-byte signed configuration layout stay unchanged. There are 14 capability bits but still 13 state fields (0–12). Never write `state.values[13]` or treat an execution acknowledgement as a lid-position sensor reading.

The P4 reference marks seat control as DEMO: it displays a simulated result and operates no seat hardware. Replace its driver for production. Test missing permission, disconnection, repeated taps, rejection, timeout, stale-session replies and actual opening. Never automatically resend a release after reconnecting.
