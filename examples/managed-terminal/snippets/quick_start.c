#include "../main/board_driver.h"
#include <stdio.h>

/* Storage/BLE BSP initialization is done by the caller (NVS on ESP-IDF). */
moto_result example_start(moto_terminal **out) {
    moto_terminal_config c;
    moto_terminal_config_init(&c);
    board_fill_port(&c.port);
    c.hardware_revision = "YOUR-BOARD-REV";
    c.firmware_version = "1.0.0";
    c.width = 1024; c.height = 600; /* Replace with actual LCD dimensions. */
    moto_terminal_error error;
    moto_result rc = moto_terminal_create(&c, out, &error);
    if (rc != MOTO_OK) {
        printf("SDK %s: %s (%s)\n", moto_terminal_stage_name(error.stage),
               error.detail, moto_terminal_result_name(error.code));
        return rc;
    }
    uint8_t advertisement[31];
    rc = moto_terminal_advertising(*out, advertisement);
    if (rc == MOTO_OK) rc = moto_terminal_start(*out);
    if (rc == MOTO_OK && board_start_ble(*out, advertisement) != 0) rc = MOTO_IO;
    if (rc != MOTO_OK) {
        board_stop_ble();
        while (moto_terminal_stop(*out, 1000) == MOTO_AGAIN) { }
        (void)moto_terminal_destroy(*out); *out = NULL;
    }
    return rc;
}
/* Call repeatedly from ONE application task; callbacks run on this task. */
moto_result example_ui_step(moto_terminal *t) {
    moto_terminal_event e;
    moto_result rc = moto_terminal_next_event(t, &e, 100);
    if (rc != MOTO_OK) return rc; /* AGAIN: no work, continue your loop. */
    switch (e.kind) {
    case MOTO_EVENT_PAIRING_PIN: board_show_pin(e.value, e.target); break;
    case MOTO_EVENT_PHONE_STATE: board_theme(e.data.phone.theme); break;
    case MOTO_EVENT_PAIRING_CLEARED: board_clear_pin(); break;
    case MOTO_EVENT_CLOSED:
        board_clear_pin(); board_stop_video(); board_stop_hotspot(); break;
    default: break;
    }
    return MOTO_OK;
}
