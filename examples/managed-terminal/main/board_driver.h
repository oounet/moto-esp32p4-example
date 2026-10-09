#ifndef CUSTOMER_BOARD_DRIVER_H
#define CUSTOMER_BOARD_DRIVER_H
#include <moto_freertos/terminal.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Only this board layer knows your flash layout, network stack and LCD/decoder drivers. */
void board_fill_port(moto_terminal_port *port);
int board_start_connection(moto_terminal *);
void board_stop_connection(void);
void board_show_pin(uint32_t pin, uint32_t valid_seconds);
void board_clear_pin(void);
void board_theme(enum moto_theme);
/* Complete only when AP/IP are usable. 0 success, nonzero actual failure. */
int board_start_hotspot(moto_terminal_hotspot *actual);
void board_stop_hotspot(void);
/* Optional factories: implement BSP callbacks and limits before enabling. */
moto_ad_port board_advertising_port(void);
moto_terminal_bulk_port board_bulk_port(void);
/* Save a COPY of e with this listener; bind real AP/IP and TCP port before success. */
int board_start_bulk_hotspot(const moto_terminal_event *e, moto_terminal_hotspot *actual);
moto_result board_on_tcp_connected(const moto_terminal_event *saved, uint64_t *connection);
moto_result board_on_tcp_rx(uint64_t connection, const uint8_t *, size_t);
void board_on_tcp_disconnected(uint64_t connection);
/* Driver prepares decoder/UDP/display resources for these exact parameters.
 * No frame is accepted before start. Video APIs are serialized on video task. */
int board_prepare_video(moto_terminal *, const moto_terminal_event *prepare);
void board_start_video(const moto_terminal_event *start);
void board_stop_video(void);
/* TCP control callbacks call these; preserve/retry FULL inputs, don't replay OK data. */
moto_result board_on_connection_connected(uint64_t *connection);
moto_result board_on_connection_rx(uint64_t connection, const uint8_t *, size_t);
void board_on_connection_disconnected(uint64_t connection);
#ifdef __cplusplus
}
#endif
#endif
