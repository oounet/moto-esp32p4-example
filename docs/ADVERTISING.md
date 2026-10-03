# PNG 广告接线 / PNG advertising wiring

先导入 SDK。下列配置在终端 `create` 前完成，端口回调都由同一个 SDK 任务调用。完整实现位于 `examples/managed-terminal/snippets/advertising.c` 和 `ad_receipts.c`，这两个文件已加入示例构建。

Import SDK first. Configure before terminal creation; the SDK task serializes callbacks. The two source files above are compiled by the managed example.

```c
#include "advertising.h"
#include "ad_receipts.h"

static example_ad_receipts receipts; /* zero initialized; NVS already initialized */
static uint8_t image[64 * 1024];
static example_ad_store store;

/* BSP functions: user points to receipts in this simple wiring; display state
 * belongs to your UI task. COPY bytes before returning from show_png. */
int board_show_png(void *user, const uint8_t *bytes, size_t n, const moto_ad_media *media);
void board_stop_png(void *user);
bool board_is_safe_idle(void *user);

int configure_png(moto_terminal_config *c) {
    int rc = example_receipts_open(&receipts);
    if (rc != MOTO_OK) return rc;
    store = (example_ad_store){
        .user = &receipts, .buffer = image, .capacity = sizeof image,
        .lookup = example_receipts_lookup,
        .commit_receipt = example_receipts_commit,
        .consume_once = example_receipts_consume,
        .show_png = board_show_png, .stop_display = board_stop_png,
        .safe_idle = board_is_safe_idle,
    };
    rc = example_ad_port(&store, 1024, 600, &c->advertising);
    if (rc != MOTO_OK) example_receipts_close(&receipts);
    return rc;
}
```

这是初始化片段，三个 `board_*` 函数要实现自己的 PNG 解码/显示及车辆安全状态读取，不是 SDK API。只在所有驱动都存在时调用；创建失败也清理存储。停止并销毁终端后，先停止播放器、释放 `store.hash`，再 `example_receipts_close(&receipts)`。缓冲区与上下文在此之前不能释放。重复初始化前先完成清理。

This is initialization code. Implement the three BSP functions for actual PNG decoding/display and safe-idle state; they are not SDK APIs. Enable only with real drivers. Clean up on create failure too. After stopping and destroying the terminal, stop the player, free `store.hash`, then close receipts. Keep contexts/buffer alive until then; clean up before reinitialization.

NVS 示例的每条 blob 同时保存广告 UUID、哈希和消费位；commit 成功才返回成功。消费位在播放前写入；掉电后可能漏播，但不会重复播放。写入失败状态不明确时停止播放，重启/重开存储再读取结果。64 条容量满时明确失败，不自动驱逐旧回执。量产需要约定有效期/保留策略、容量和存储耐久性；不要解绑时清除回执。

Each NVS blob contains UUID, hash and consumed bit. Success requires commit; consumption precedes playback, allowing a missed play on power loss but preventing replay. Ambiguous failures stop playback until reopening storage. The sample refuses new entries at 64 receipts. Production needs retention, capacity and endurance design. Never clear receipts on unbind.

BLE 小图验证完成后，再接 `bulk.c` 的 TCP 回调，填写 `c.bulk` 和 `c.wifi_advertising=true`。SDK 管理 TLS 和收发背压；显示、Wi-Fi/TCP 和安全状态仍由应用负责。例子只声明 PNG，不声明未实现的 GIF/视频支持。

After small PNGs work over BLE, wire `bulk.c` TCP callbacks, set `c.bulk` and `c.wifi_advertising=true`. SDK owns TLS/backpressure; the application owns display, Wi-Fi/TCP and safety state. This example declares PNG only.
