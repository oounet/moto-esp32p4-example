# Proximity unlock and vehicle power

[中文](PROXIMITY_AND_POWER.md)

The full application uses ABI 8 core APIs. Model configuration grants `vehicle.proximity.unlock` (bit 11) and `vehicle.power.control` (bit 12) independently through the APP's signed configuration. Local support bits cannot grant cloud permissions.

## Proximity unlock

`main/terminal.c` registers `moto_proximity_port` on the separate BLE companion. Every 500 ms it reads RSSI from that NimBLE connection and calls `moto_proximity_sample`. Failed reads supply an invalid sample to interrupt the approach decision. The SDK owns filtering, calibration messages and timeouts; the application does not estimate distance itself.

The user starts stable sampling at about two metres through the APP. Activation succeeds only after the terminal confirms durable storage. `proximity_load/save` stores an NVS record scoped by a hash of the owner and APP installation identity. Read/commit failures return errors. Body obstruction, orientation and surroundings affect RSSI; it is not precise two-metre ranging. Test approach, departure, lingering, disconnect and manual locking on the target board.

The SDK-local `MOTO_PROXIMITY_UNLOCK_TRIGGER` event (56) enters the control queue; it is not a phone-sendable unlock command. Jobs retain origin, connection generation, session and deadline, and complete on their original SDK instance. Wi-Fi disconnect does not cancel BLE-origin work. Disconnecting that BLE connection prevents stale work from entering a new session. Successful manual locking calls `moto_proximity_reset` to require departure before another approach.

## Vehicle power

The primary Wi-Fi instance receives `MOTO_POWER_SET` (55): `target=0`, `value=0` off or `1` on. `main/terminal_control.h` explicitly maps commands to state fields; never derive fields by subtracting message numbers.

This reference only demonstrates backlight power. Wi-Fi, BLE, LCD scanout and telemetry remain active so the APP can turn it on again. No real vehicle power or lock actuator is connected. Production must replace the execution queue, wait for observed controller results before completing commands, and report actual state: lock is field 3, range remains field 11, power is field 12. Mark unavailable readings unknown.

## Entry points and acceptance

`examples/managed-terminal` supports primary power commands. Wire proximity through a separate core BLE instance as in the full application. Never complete BLE work through a Wi-Fi terminal handle. Android notifications use that BLE path too; copy them in the callback with `moto_event_get_notification`.

See [validation](VALIDATION.md) for builds and host checks. Independently test calibration persistence, stale-session isolation, actual execution failures, timeouts, duplicate actions and phone background reconnection. Compilation does not establish physical unlocking or power-off.
