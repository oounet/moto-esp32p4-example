#include "board_driver.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>

/* Retry only submissions that were not accepted. SDK processes accepted completions
 * on its own task, including core TX backpressure. No second completion queue here. */
static moto_result complete(moto_terminal *t, const moto_terminal_event *e, bool success) {
    moto_result r;
    do {
        r = moto_terminal_complete(t, e, success ? MOTO_SUCCEEDED : MOTO_FAILED, success ? 0 : 1);
        if (r == MOTO_FULL)
            vTaskDelay(pdMS_TO_TICKS(10));
    } while (r == MOTO_FULL);
    return r;
}
void app_main(void) {
    /* Do not erase NVS automatically on failure: it may contain License/owner data. */
    if (nvs_flash_init() != ESP_OK) {
        puts("NVS initialization failed; preserve provisioned data");
        return;
    }
    moto_terminal_config c;
    moto_terminal_config_init(&c);
    board_fill_port(&c.port);
    c.hardware_revision = "YOUR-BOARD-REV";
    c.firmware_version = "1.0.0";
    /* ABI 3: enable only after implementing BSP storage/receipts/decoders.
     * c.advertising = board_advertising_port();  // BLE first
     * c.bulk = board_bulk_port();               // optional TCP byte transport
     * c.wifi_advertising = true;                // requires both ports above
     * Routes/media/instruments are opt-in; see FreeRTOS_功能接入.md. */
    c.width = 1024;
    c.height = 600;
#if MOTO_DEMO_MJPEG
    /* Explicit real decoder target; H264 stays disabled and its fields stay zero. */
    c.mjpeg = (moto_terminal_video_limit){.enabled = true, .fps = 13, .bitrate = 12000000};
#endif
    moto_terminal *t = NULL;
    moto_terminal_error error;
    if (moto_terminal_create(&c, &t, &error) != MOTO_OK) {
        printf("SDK %s: %s (%s)\n", moto_terminal_stage_name(error.stage), error.detail,
               moto_terminal_result_name(error.code));
        return;
    }
    uint8_t advertising[31];
    if (moto_terminal_advertising(t, advertising) != MOTO_OK || moto_terminal_start(t) != MOTO_OK) {
        moto_terminal_destroy(t);
        return;
    }
    if (board_start_ble(t, advertising) != 0) {
        board_stop_ble();
        while (moto_terminal_stop(t, 1000) == MOTO_AGAIN) {
        }
        moto_terminal_destroy(t);
        return;
    }
    /* This is your existing application/UI task. SDK's own task keeps TLS running. */
    for (;;) {
        moto_terminal_event e;
        if (moto_terminal_next_event(t, &e, 100) != MOTO_OK)
            continue;
        switch (e.kind) {
        case MOTO_EVENT_PAIRING_PIN:
            board_show_pin(e.value, e.target);
            break;
        case MOTO_EVENT_PAIRING_CLEARED:
            board_clear_pin();
            break;
        case MOTO_EVENT_PHONE_STATE:
            board_theme(e.data.phone.theme);
            break;
        case MOTO_EVENT_WIFI: {
            moto_terminal_hotspot ap = {0};
            if ((e.data.wifi.purpose == 0 ? board_start_hotspot(&ap)
                                        : board_start_bulk_hotspot(&e, &ap)) != 0) {
                (void)complete(t, &e, false);
                break;
            }
            moto_result r;
            do {
                r = e.data.wifi.purpose == 0 ? moto_terminal_wifi_ready(t, &e, &ap)
                                            : moto_terminal_bulk_ready(t, &e, &ap);
                if (r == MOTO_FULL)
                    vTaskDelay(pdMS_TO_TICKS(10));
            } while (r == MOTO_FULL);
            if (r != MOTO_OK)
                board_stop_hotspot();
            break;
        }
        case MOTO_EVENT_STREAM_PREPARE:
            if (complete(t, &e, board_prepare_video(t, &e) == 0) != MOTO_OK)
                board_stop_video();
            break;
        case MOTO_EVENT_STREAM_START:
            board_start_video(&e);
            break;
        case MOTO_EVENT_STREAM_STOP:
        case MOTO_EVENT_NAV_END:
            board_stop_video();
            break;
        case MOTO_EVENT_WIFI_STOP:
            board_stop_hotspot();
            break;
        case MOTO_EVENT_CLOSED:
            board_stop_video();
            board_clear_pin();
            board_stop_hotspot();
            break;
        case MOTO_EVENT_VEHICLE_COMMAND:
            /* This demo has no CAN/GPIO control implementation: report real failure. */
            (void)complete(t, &e, false);
            break;
        case MOTO_TERMINAL_EVENT_ERROR:
            printf("SDK %s: %s\n", moto_terminal_stage_name(e.data.error.stage),
                   moto_terminal_result_name(e.data.error.code));
            break;
        default:
            break;
        }
    }
}
