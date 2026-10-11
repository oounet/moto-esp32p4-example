# Proximity unlock

[中文](PROXIMITY_UNLOCK.md)

The full application uses ABI 8 core APIs. Model authorization grants `vehicle.proximity.unlock` (bit 11) through verified signed configuration. Local support bits cannot grant cloud permissions.

## Proximity unlock

`main/terminal.c` registers `moto_proximity_port` on the separate BLE companion. Every 500 ms it reads RSSI from that NimBLE connection and calls `moto_proximity_sample`. Failed reads supply an invalid sample to interrupt the approach decision. The SDK owns filtering, calibration messages and timeouts; the application does not estimate distance itself.

The user starts stable sampling at about two metres through the APP. Activation succeeds only after the terminal confirms durable storage. `proximity_load/save` stores an NVS record scoped by a hash of the owner and APP installation identity. Read/commit failures return errors. Body obstruction, orientation and surroundings affect RSSI; it is not precise two-metre ranging. Test approach, departure, lingering, disconnect and manual locking on the target board.

The SDK-local `MOTO_PROXIMITY_UNLOCK_TRIGGER` event (56) enters the control queue; it is not a phone-sendable unlock command. Jobs retain origin, connection generation, session and deadline, and complete on their original SDK instance. Wi-Fi disconnect does not cancel BLE-origin work. Disconnecting that BLE connection prevents stale work from entering a new session. Successful manual locking calls `moto_proximity_reset` to require departure before another approach.

## Entry points and acceptance

Wire proximity through a separate core BLE instance as in the full application. Never complete BLE work through a Wi-Fi terminal handle. Reference lock state is DEMO; production requires a real actuator and controller-confirmed results. Report observed lock state in field 3, marking unavailable readings unknown.

See [validation](VALIDATION.md) for builds and host checks. Test calibration persistence, stale-session isolation, actual execution failures, timeouts, duplicate actions and phone background reconnection on hardware. Compilation does not establish physical unlocking. See [vehicle power](VEHICLE_POWER.en.md) for power commands.
