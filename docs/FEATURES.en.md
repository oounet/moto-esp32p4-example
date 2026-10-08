# Feature map

[中文](FEATURES.md) · [English](FEATURES.en.md)

Start with the full board application. For a new BSP use the separate managed example; every snippet is compiled by its CMake target. Snippets are opt-in helpers, not all active by default. SDK internals remain in proprietary archives.

| Feature | Managed example | Board implementation | State / work required |
| --- | --- | --- | --- |
| Startup, lifetime, errors | [quick_start.c](../examples/managed-terminal/snippets/quick_start.c) / [api.c](../examples/managed-terminal/snippets/api.c) | [main/terminal.c](../main/terminal.c) | Board application wired; facade includes shutdown and queue retry |
| License and pairing | [license.c](../examples/managed-terminal/snippets/license.c) | [main/factory_license.c](../main/factory_license.c) / [main/factory_usb.c](../main/factory_usb.c) | Requires device license and trusted public key; NVS persistence |
| BLE, PIN, reconnect | [bluetooth.c](../examples/managed-terminal/snippets/bluetooth.c) | [main/terminal.c](../main/terminal.c) | P4+C6 NimBLE; root supports authenticated connection handoff |
| Theme and phone battery | [main/main.c](../examples/managed-terminal/main/main.c) | [main/terminal_board.c](../main/terminal_board.c) / [main/ui/electric_ui.c](../main/ui/electric_ui.c) | Reference UI wired; change layout on UI task |
| Vehicle control, telemetry, faults | [vehicle.c](../examples/managed-terminal/snippets/vehicle.c) / [faults.c](../examples/managed-terminal/snippets/faults.c) | [main/terminal_board.c](../main/terminal_board.c) | Explicit DEMO; replace with real CAN/UART |
| Navigation HUD | [video.c](../examples/managed-terminal/snippets/video.c) | [main/hud_view.c](../main/hud_view.c) / [main/terminal_board.c](../main/terminal_board.c) | Turn/distance/road rendering remains active during video |
| Navigation video and standalone mirror | [video.c](../examples/managed-terminal/snippets/video.c) | [main/terminal_video.c](../main/terminal_video.c) | AP, UDP, H.264/MJPEG, presentation receipts; mirror does not start navigation |
| Android notifications | [notifications.c](../examples/managed-terminal/snippets/notifications.c) | [main/notice_view.c](../main/notice_view.c) / [main/terminal_board.c](../main/terminal_board.c) | Display, removal, actions; phone permission required |
| iPhone ANCS | [notifications.c](../examples/managed-terminal/snippets/notifications.c) | [main/ancs_adapter.c](../main/ancs_adapter.c) | GATT subscription adapter; iOS permission and actual readiness required |
| Calls and HFP | [notifications.c](../examples/managed-terminal/snippets/notifications.c) | — | Compiled example; external HFP audio hardware/driver required |
| Music controls | [optional_features.c](../examples/managed-terminal/snippets/optional_features.c) | — | Compiled example; phone media session and UI required |
| Route and position extension | [optional_features.c](../examples/managed-terminal/snippets/optional_features.c) | — | Revision matching example; requires matching APP events, distinct from video |
| Instrument selection | [optional_features.c](../examples/managed-terminal/snippets/optional_features.c) | [main/terminal_board.c](../main/terminal_board.c) / [main/ui/electric_ui.c](../main/ui/electric_ui.c) | Two basic layouts; persist/confirm after rendering |
| PNG advertising and once-only playback | [advertising.c](../examples/managed-terminal/snippets/advertising.c) / [ad_receipts.c](../examples/managed-terminal/snippets/ad_receipts.c) | — | Compiled SHA-256 and NVS receipts; wire PNG display/safe-idle; off in root |
| Wi-Fi bulk transfer | [bulk.c](../examples/managed-terminal/snippets/bulk.c) | — | Compiled template; SDK owns TLS, BSP owns TCP; dispatch by purpose |
| OTA transport | [ota.c](../examples/managed-terminal/snippets/ota.c) | — | Explicit simulator: hash/resume/status; no Flash write, reboot or install |
| Validation and troubleshooting | [testing.c](../examples/managed-terminal/snippets/testing.c) | [tools/test_host.py](../tools/test_host.py) | Build and memory/color/UI checks; board acceptance still required |
| Offline maps | [Integration](OFFLINE_MAPS.en.md) | `main/route_map_*.c` | Root project: compressed transfers, bounded window and BLE positions; requires updated SDK |

## Suggested order

1. Import SDK, build, provision, pair and switch theme.
2. Replace DEMO telemetry/control with confirmed hardware results; test stale sessions and queue-full retry.
3. Validate HUD, then video; confirm displayed-frame receipts and stop/reconnect behavior.
4. Add notifications, external HFP, music or route features only when their drivers exist.
5. For ads, wire `advertising.h` to `ad_receipts.h` and a PNG display; commit consumption before playback. The NVS sample holds 64 durable receipts and fails when full; design retention with the service before production. Never clear receipts on unbind.
6. Use `ota.h` only for transport development. Production needs signed-manifest verification, OTA partitions, durable offset, inactive-slot writes, boot verification and rollback. The root factory partition layout is not a production OTA layout.

See [provisioning](PROVISIONING.md), [architecture](ARCHITECTURE.md), and [validation](VALIDATION.md).
