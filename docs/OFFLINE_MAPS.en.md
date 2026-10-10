# Offline maps

[中文](OFFLINE_MAPS.md)

## Using offline maps

In the APP, open Profile → Offline maps, search for a city and download it. Connect to device Wi-Fi before navigation, keep the APP in the foreground and wait for device maps to be ready before locking the phone. The phone stores the city; the instrument receives only the area around the route.

Live navigation video takes priority whenever available. When video stops, the instrument draws the received basemap and route using Wi-Fi position updates. Video resumes automatically when available again. Cached maps do not provide independent offline routing: destination search, route planning and rerouting still use the phone navigation service.

## What is stored

| Location | Content | Lifetime |
| --- | --- | --- |
| Phone | City data, fonts, style and generated route images | Managed with download, pause, resume and delete actions |
| Instrument | A bounded route window, the full route and latest position | Temporary memory; cleared when navigation or the authenticated session ends |

Zaoyang currently has a bundled vector city package. Saving it avoids downloading its city data again. Other cities use the existing map-service city cache. The Zaoyang package is about 28 MiB; generated route images add to storage. Detail and coverage depend on the source, so city sizes and road coverage are not uniform.

Route preparation reads the saved cache. Vector maps are rendered into route images in the foreground before selecting a device window. Limited device capacity shortens the forward coverage without reducing display zoom 17 or image quality. The entire city is never sent to the board. Return to the APP to complete maps if the route leaves the downloaded city or the download is incomplete.

## Board integration

The [ESP32-P4 open reference application](https://github.com/oounet/moto-esp32p4-example) implements this in `main/route_map_*.c` using public low-level SDK C APIs. Use a matching **2026-10-08 revision 11 or newer binary package**. The `moto_map_port` structure changed: update headers and archives together.

Maps are wired into the full P4 root project. The separate ABI 6 `examples/managed-terminal` facade does not yet expose the map port. Do not attach concurrent low-level calls to its private SDK instance or start a second terminal.

| File | Responsibility |
| --- | --- |
| `route_map_transport.c` | Nonblocking TCP, authenticated TLS I/O, timeout and cancellation |
| `route_map_store.c` | Bounded staging, validated map replacement, JPEG cache, route and position events |
| `route_map_compression.c` | MZP1 decompression, length and CRC checks |
| `route_map_view.c` | RGB565 basemap, route and position rendering |
| `terminal.c` / `terminal_board.c` | SDK owner-thread wiring and video/local-map selection |

Configure the port before creating the SDK:

```c
#include "route_map_store.h"

// cfg is the moto_config supplied to moto_create.
cfg.map = terminal_map_port();
cfg.route_navigation = cfg.map.max_bytes != 0;
```

On the SDK owner thread, forward only the Wi-Fi business instance's authenticated owner events to `terminal_map_event(event)` and periodically call `terminal_map_poll(sdk)`. The example uses the NimBLE host at 20 ms intervals and bounds each TCP read batch. Never block the SDK owner thread indefinitely. The display task calls `terminal_map_draw`; a mutex protects shared state. Callback pointers are borrowed, so copy route and position values before retaining them.

Maps share the navigation access point but use TCP `192.168.4.1:5003`; video uses UDP 5001. Working video does not prove the map port is reachable. TLS credentials come from the current encrypted Wi-Fi business session. Navigation, route, owner or session changes invalidate the old transfer. Never log credentials or use universal fixed keys.

## Compressed continuous transfer

The phone losslessly compresses the map package and sends it continuously, without waiting for an application acknowledgement for every chunk. TCP/TLS still provides reliable delivery, flow control and authentication. The device acknowledges completion only after receiving, validating, decompressing and committing the whole package.

- MTP1 contains route geometry and 256×256 map images. MZP1 wraps a zlib/DEFLATE stream with the decoded length and CRC; there are no ZIP paths to extract.
- If compression saves too little, raw MTP1 uses the same continuous transport. Image quality and zoom remain unchanged.
- Reconnect by querying the device's received offset; locally queued bytes are not a resume position.
- The P4 example limits both wire and decoded packages to 4 MiB. Old/new maps and decompression buffers can overlap. Allocation reserves additional PSRAM; the JPEG cache holds at most 20 tiles. No Flash writes are required.
- Failed validation preserves the usable map. Expired positions stop moving and show a paused state. Missing tiles use a gray background while valid route and HUD data remain visible.

## Testing and troubleshooting

1. **Video works but maps are not ready:** check TCP 5003, the local subnet, phone local-network permission and map TLS state, separately from video UDP.
2. **A saved city still needs preparation:** city data and route images are different. A new route may need local rendering; keep the APP foregrounded. Generated images are reusable.
3. **Only HUD or a route after locking:** confirm transfer completion, fresh Wi-Fi positions and city coverage. Locking during initial preparation can interrupt rendering.
4. **Missing maps on a long route or after rerouting:** check moving-window updates, Wi-Fi, storage and cache coverage. New areas may require returning to the foreground.
5. **Stale display after navigation ends:** clear map and position state for NAV_END, CLOSED, UNPAIR and owner handoff, and reject late callbacks from prior generations.

Stationary iPhone-to-P4 transfer has been tested and the user confirmed a substantial speed improvement. This is not a fixed throughput guarantee. Android hardware, sustained riding, extended lock-screen use, rerouting, leaving the cache, reconnect/resume and memory pressure require target-product acceptance. Builds do not establish those results.

## Map credits

Credits are available under Map credits on the APP offline maps page rather than repeated across the instrument. Vector maps use [© OpenStreetMap contributors](https://www.openstreetmap.org/copyright) data and [Protomaps](https://protomaps.com); raster caches use [Tianditu](https://www.tianditu.gov.cn). Retain the applicable licenses and source information for the data, services and distribution you use.
