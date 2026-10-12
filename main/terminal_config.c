#include "terminal_config.h"
void terminal_configure_board(moto_config *config, bool video_ready)
{
    /* Adapter inventory, not authorization. SDK exposes no enabled vehicle bits
       until it verifies this owner's signed cloud model configuration. */
    config->capabilities.revision = 1;
    config->capabilities.supported = MOTO_CAP(MOTO_DISPLAY) | MOTO_CAP(MOTO_LOCK) |
        MOTO_CAP(MOTO_VOLUME) | MOTO_CAP(MOTO_LIGHT) | MOTO_CAP(MOTO_HORN) |
        MOTO_CAP(MOTO_BATTERY) | MOTO_CAP(MOTO_CHARGING) | MOTO_CAP(MOTO_MILEAGE) | MOTO_CAP(MOTO_FAULT) |
        MOTO_CAP(MOTO_PROXIMITY_UNLOCK) | MOTO_CAP(MOTO_POWER) | MOTO_CAP(MOTO_SEAT_CONTROL);
    config->capabilities.width = 1024; config->capabilities.height = 600;
    config->capabilities.rotation = 0;
    config->capabilities.controls[0] = (moto_control_limit){1, 3, 0, 1, 1};
    config->capabilities.controls[1] = (moto_control_limit){1, 1, 0, 100, 1};
    config->capabilities.controls[2] = (moto_control_limit){1, 7, 0, 2, 1};
    config->capabilities.controls[3] = (moto_control_limit){1, 1, 0, 1, 1};
    config->simulated_capabilities = config->capabilities.supported & ~MOTO_CAP(MOTO_DISPLAY);
    config->video = video_ready;
    config->video_codecs = MOTO_VIDEO_CODEC_MASK(MOTO_VIDEO_H264) | MOTO_VIDEO_CODEC_MASK(MOTO_VIDEO_MJPEG);
    config->video_mjpeg_max_fps = 13; config->video_mjpeg_max_bitrate = 12000000;
    config->video_max_fps = 12; config->video_max_bitrate = 2000000;
}
