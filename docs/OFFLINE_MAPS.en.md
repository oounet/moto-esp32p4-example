# Offline maps

[中文](OFFLINE_MAPS.md) · [Vector rendering and host tests](LEAN_VECTOR_MAP.md)

## Use and boundaries

Download regional road data from Profile → Offline maps in the APP. The phone stores RVS1 geometry and prepares bounded RVP1 windows for the route, optionally compressed as RVZ1. The instrument does not store entire cities or generate map images, fonts or styles. Cached roads do not provide standalone offline routing; destination search, planning and rerouting remain phone services.

Connect vehicle Wi-Fi and keep the APP foregrounded until the current device map is ready. Live video takes priority; when it stops, the instrument draws confirmed road windows, the route and actual positions. Incomplete coverage may still show valid route/position geometry, but must remain marked incomplete. First downloads, background riding, lock-screen use, window updates and LCD output require target-phone and terminal validation.

## Board integration

Import matching **SDK 1.0.0 / revision 27 / FreeRTOS ABI 8 or newer** headers, archives and metadata together. Maps are wired into the full P4 root application. Do not concurrently call the managed example's private core instance or start a second terminal to reuse maps.

| File | Responsibility |
| --- | --- |
| `route_map_transport.c` | TCP 5003, authenticated TLS, timeout and cancellation |
| `route_map_store.c` | Bounded staging, navigation/route/position identity, expiry and display selection |
| `route_vector_store.c` | Worker validation, immutable snapshots, short locks and reference counts |
| `route_map_compression.c` | RVZ1 decompression, decoded length and CRC checks |
| `route_vector_view.c` | RGB565 roads, route and arrow rendering, incomplete-block propagation |

```c
#include "route_map_store.h"
cfg.map = terminal_map_port();
cfg.route_navigation = cfg.map.max_bytes != 0;
```

On the SDK owner thread, forward current authenticated primary Wi-Fi events to `terminal_map_event` and call `terminal_map_poll` periodically. The display task uses `terminal_map_draw`; copy borrowed callback data before retaining it. Video UDP 5001 readiness does not establish map readiness. TLS authorization comes from the current business session; never log credentials or use universal fixed keys.

## Transfer, memory and recovery

Only RVP1/RVZ1 vector windows are accepted, up to 1 MiB per window, without JPEG map caches or raster fallback. Before publishing a snapshot and acknowledging readiness, validate SHA, navigation/route/window identities, parser budgets and decoded length. Known-empty blocks count as coverage; missing blocks do not. Continuous TCP/TLS transfer remains in use, and resume offsets come from terminal confirmation.

Failed validation retains the usable snapshot. Expired positions freeze at the last actual location with a gray arrow; never extrapolate movement. Navigation end or owner/session changes must clear old state and reject late results. For incomplete maps, inspect TCP 5003, TLS, data coverage, window identity and memory. Successful builds do not establish device behavior.

The APP displays map credits and licensing; the instrument does not repeat an overlay. Preserve the actual source attribution and license notices supplied with the data.
