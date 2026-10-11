# Vehicle power

[中文](VEHICLE_POWER.md)

Model authorization grants `vehicle.power.control` (bit 12) through verified signed configuration. Use matching ABI 8 headers and archives.

## Vehicle power

The primary Wi-Fi instance receives `MOTO_POWER_SET` (55): `target=0`, `value=0` off or `1` on. `main/terminal_control.h` explicitly maps commands to state fields; never derive fields by subtracting message numbers.

This reference only demonstrates backlight power. Wi-Fi, BLE, LCD scanout and telemetry remain active so the APP can turn it on again. No real vehicle power actuator is connected. Production must replace the execution queue, wait for observed controller results before completing commands, and report actual state: range remains field 11, power is field 12. Mark unavailable readings unknown.

## Entry points and acceptance

Both the full application and `examples/managed-terminal` support primary power commands. Check the original session, deadline and safety interlocks before execution. Complete on the original instance; retrying result delivery must not repeat physical actions.

Test actual power-on/off, rejection, timeout, disconnection and the next power-on. Production hardware must retain standby power and communication to receive a power-on command. See [validation](VALIDATION.md) for builds and host checks, and [proximity unlock](PROXIMITY_UNLOCK.en.md) for the separate lock feature.
