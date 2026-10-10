/* OS/network/decoder adapter; moto-sdk owns authentication and the UDP v2 parser. */
#include "terminal_video.h"
#include "video_color.h"
#include "cockpit_map_mask.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_h264_dec_sw.h"
#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "moto/video.h"
#include "nvs.h"

enum { WIDTH = 1024, HEIGHT = 600, PORT = 5001, RX_SLOTS = 96 };
typedef struct {
    uint64_t arrived_ms;
    uint32_t address;
    unsigned epoch;
    uint16_t size;
    uint8_t data[1401];
} packet;
typedef struct {
    bool manual_ap;
    char ssid[33], password[33];
    unsigned epoch;
    enum moto_event_kind kind;
    uint64_t session, request;
    moto_stream stream;
} work;
typedef struct {
    work job;
    bool success;
    char ssid[33], password[33];
} completion;
static const char *TAG = "MotoVideo";
static QueueHandle_t jobs, completions;
static QueueHandle_t packets;
static StaticQueue_t packet_queue;
static atomic_int receive_socket = ATOMIC_VAR_INIT(-1);
static atomic_uint packet_overflows, lcd_frames;
static SemaphoreHandle_t pixels_mutex;
static atomic_uint epoch, peer_ip, keyframe_stream;
static atomic_bool playing, preview_mode;
static atomic_bool picture_ready;
static atomic_uint picture_time;
static atomic_bool ap_radio_ready;
static atomic_uint playing_epoch;
static portMUX_TYPE peer_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t peer_mac[6];
static bool peer_associated;
static uint16_t *rgb;
/* pixels_mutex protects the last complete image, independent of live stream state. */
static bool last_frame_valid;
static bool new_pixels;
static uint32_t rgb_stream, presented_stream;
static uint64_t rgb_frame, presented_frame;
static bool new_presented;
static unsigned presented_epoch;
/* Everything below is private to the video worker. */
static int udp = -1;
static bool wifi_initialized, ap_started, manual_ap;
static char ap_ssid[33], ap_password[33];
static esp_netif_t *ap;
static portMUX_TYPE credentials_lock = portMUX_INITIALIZER_UNLOCKED;
bool terminal_video_wifi_credentials(char ssid[33], char password[33]) {
    if (!atomic_load(&ap_radio_ready)) return false;
    portENTER_CRITICAL(&credentials_lock);
    memcpy(ssid, ap_ssid, 33); memcpy(password, ap_password, 33);
    portEXIT_CRITICAL(&credentials_lock);
    return ssid[0] && password[0];
}

static moto_video *receiver;
static esp_h264_dec_handle_t decoder;
static jpeg_decoder_handle_t jpeg_decoder;
static uint8_t *jpeg_pixels;
static size_t jpeg_pixels_size;
static enum moto_video_codec video_codec;
static esp_h264_dec_param_sw_handle_t decoder_params;
static unsigned worker_epoch;
static uint64_t peer_token;
static uint32_t stream_id;
static bool need_idr = true;
static uint64_t decoded_frames;
static uint64_t received_packets, filtered_packets, peer_probe_at;
static uint64_t decode_time_us, decode_calls;
static uint64_t convert_time_us, convert_calls;
static uint64_t fps_at, fps_decoded;
static unsigned fps_presented;
typedef struct { char ssid[33], password[33]; } saved_ap;
static bool valid_ap(const char *ssid, const char *password)
{
    size_t a = strnlen(ssid, 33), b = strnlen(password, 33);
    if (!a || a > 32 || b < 8 || b > 32) return false;
    for (size_t i = 0; i < a; ++i)
        if (!((ssid[i] >= 'a' && ssid[i] <= 'z') || (ssid[i] >= 'A' && ssid[i] <= 'Z') ||
              (ssid[i] >= '0' && ssid[i] <= '9') || ssid[i] == '-' || ssid[i] == '_')) return false;
    for (size_t i = 0; i < b; ++i)
        if (!((password[i] >= 'a' && password[i] <= 'z') || (password[i] >= 'A' && password[i] <= 'Z') ||
              (password[i] >= '0' && password[i] <= '9'))) return false;
    return true;
}
static bool load_ap(completion *done)
{
    nvs_handle_t handle;
    if (nvs_open("manual_wifi", NVS_READONLY, &handle) != ESP_OK) return false;
    saved_ap value = {0}; size_t size = sizeof value;
    bool ok = nvs_get_blob(handle, "credentials", &value, &size) == ESP_OK && size == sizeof value &&
              valid_ap(value.ssid, value.password);
    nvs_close(handle);
    if (ok) { memcpy(done->ssid, value.ssid, sizeof value.ssid); memcpy(done->password, value.password, sizeof value.password); }
    memset(&value, 0, sizeof value); return ok;
}
static bool save_ap(const work *job)
{
    nvs_handle_t handle;
    if (nvs_open("manual_wifi", NVS_READWRITE, &handle) != ESP_OK) return false;
    saved_ap value = {0};
    memcpy(value.ssid, job->ssid, sizeof value.ssid); memcpy(value.password, job->password, sizeof value.password);
    esp_err_t rc = nvs_set_blob(handle, "credentials", &value, sizeof value);
    if (rc == ESP_OK) rc = nvs_commit(handle);
    memset(&value, 0, sizeof value); nvs_close(handle); return rc == ESP_OK;
}

static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void network_event(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void)context;
    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *assigned = data;
        atomic_store(&peer_ip, assigned->ip.addr);
        ESP_LOGI(TAG, "AP client assigned " IPSTR, IP2STR(&assigned->ip));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        atomic_store(&ap_radio_ready, true);
        ESP_LOGI(TAG, "AP radio started at 192.168.4.1");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP) {
        atomic_store(&ap_radio_ready, false);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *joined = data;
        portENTER_CRITICAL(&peer_lock);
        memcpy(peer_mac, joined->mac, sizeof peer_mac); peer_associated = true;
        portEXIT_CRITICAL(&peer_lock);
        ESP_LOGI(TAG, "AP station associated");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *left = data;
        portENTER_CRITICAL(&peer_lock);
        peer_associated = false;
        portEXIT_CRITICAL(&peer_lock);
        ESP_LOGW(TAG, "AP station disconnected reason=%u", left->reason);
        atomic_store(&peer_ip, 0);
        /* Loss invalidates every buffered picture; restart through the secure control channel. */
        atomic_store(&playing, false);
        atomic_fetch_add(&epoch, 1);
    }
}
typedef struct { ip4_addr_t ip; uint8_t mac[6]; bool matches; } peer_probe;
static void probe_peer_arp(void *context)
{
    peer_probe *probe = context;
    struct netif *netif = esp_netif_get_netif_impl(ap);
    struct eth_addr *mac = NULL; const ip4_addr_t *ip = NULL;
    if (etharp_find_addr(netif, &probe->ip, &mac, &ip) >= 0)
        probe->matches = !memcmp(mac->addr, probe->mac, sizeof probe->mac);
    else etharp_request(netif, &probe->ip);
}
static bool accepted_peer(uint32_t address)
{
    uint32_t assigned = atomic_load(&peer_ip);
    if (assigned) return address == assigned;
    /* A phone may reuse its DHCP lease after AP reboot without another DHCP event.
     * Bind only after ARP confirms the source belongs to the associated WPA2 station. */
    uint32_t host = ntohl(address);
    if ((host & 0xffffff00u) != 0xc0a80400u || (host & 255u) < 2 || (host & 255u) == 255) return false;
    uint64_t now = now_ms();
    if (now < peer_probe_at) return false;
    peer_probe_at = now + 100;
    peer_probe probe = {.ip = {.addr = address}};
    portENTER_CRITICAL(&peer_lock);
    bool associated = peer_associated;
    memcpy(probe.mac, peer_mac, sizeof probe.mac);
    portEXIT_CRITICAL(&peer_lock);
    if (!associated || tcpip_callback_wait(probe_peer_arp, &probe) != ERR_OK || !probe.matches ||
        worker_epoch != atomic_load(&epoch)) return false;
    atomic_store(&peer_ip, address);
    ESP_LOGI(TAG, "AP cached-lease peer verified " IPSTR, IP2STR(&probe.ip));
    return true;
}
static void cleanup(void)
{
    atomic_store(&playing, false);
    atomic_store(&picture_ready, false);
    /* The receiver task owns reads for this socket's entire lifetime. Keep the
     * bound socket across sessions so close/reopen cannot race a blocked read. */
    xQueueReset(packets);
    if (receiver) { moto_video_destroy(receiver); receiver = NULL; }
    if (decoder) { esp_h264_dec_close(decoder); esp_h264_dec_del(decoder); decoder = NULL; }
    /* JPEG DMA resources are reserved once at boot. Stream teardown only clears
     * ownership/pixels; reallocating descriptor chains amid Wi-Fi traffic can fail. */
    if (!manual_ap) {
        if (ap_started) { esp_wifi_stop(); ap_started = false; }
        atomic_store(&peer_ip, 0);
        memset(ap_ssid, 0, sizeof ap_ssid);
        memset(ap_password, 0, sizeof ap_password);
    }
    atomic_store(&keyframe_stream, 0);
    stream_id = 0; peer_token = 0; need_idr = true;
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    new_pixels = new_presented = false;
    xSemaphoreGive(pixels_mutex);
}
static bool open_ap(completion *done)
{
    if (!wifi_initialized) {
        esp_err_t rc = esp_netif_init();
        if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) return false;
        rc = esp_event_loop_create_default();
        if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) return false;
        ap = esp_netif_create_default_wifi_ap();
        if (!ap) return false;
        /* Keep the receiver endpoint stable across AP sessions and firmware builds. */
        esp_netif_ip_info_t ip = {0};
        IP4_ADDR(&ip.ip, 192, 168, 4, 1);
        /* Local accessory only: never claim an Internet default route. */
        IP4_ADDR(&ip.gw, 0, 0, 0, 0);
        IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
        rc = esp_netif_dhcps_stop(ap);
        if (rc != ESP_OK && rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return false;
        uint8_t disabled = 0;
        if (esp_netif_set_ip_info(ap, &ip) != ESP_OK ||
            esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &disabled, sizeof disabled) != ESP_OK ||
            esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &disabled, sizeof disabled) != ESP_OK ||
            esp_netif_dhcps_start(ap) != ESP_OK) return false;
        ESP_LOGI(TAG, "Local-only AP: DHCP gateway=none DNS=none; Internet remains on phone uplink");
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&config) != ESP_OK) return false;
        if (esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, network_event, NULL) != ESP_OK ||
            esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL) != ESP_OK) return false;
        wifi_initialized = true;
    }
    if (!ap_started) {
        bool saved = load_ap(done);
        if (!saved) {
        uint8_t random[16]; esp_fill_random(random, sizeof random);
        snprintf(done->ssid, sizeof done->ssid, "MotoP4-%02X%02X%02X", random[0], random[1], random[2]);
        for (unsigned i = 0; i < 6; ++i) snprintf(done->password + i * 2, 3, "%02x", random[i + 4]);
        memset(random, 0, sizeof random);
        }
        wifi_config_t config = {0};
        memcpy(config.ap.ssid, done->ssid, strlen(done->ssid));
        config.ap.ssid_len = strlen(done->ssid);
        memcpy(config.ap.password, done->password, strlen(done->password));
        config.ap.channel = 6; config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
        config.ap.beacon_interval = 100; config.ap.dtim_period = 2;
        config.ap.max_connection = 1; config.ap.pmf_cfg.required = false;
        esp_err_t rc = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (rc == ESP_OK) rc = esp_wifi_set_mode(WIFI_MODE_AP);
        if (rc == ESP_OK) rc = esp_wifi_set_config(WIFI_IF_AP, &config);
        wifi_config_t actual = {0};
        if (rc == ESP_OK) rc = esp_wifi_get_config(WIFI_IF_AP, &actual);
        bool matches = rc == ESP_OK && actual.ap.ssid_len == config.ap.ssid_len &&
            !memcmp(actual.ap.ssid, config.ap.ssid, config.ap.ssid_len) &&
            !memcmp(actual.ap.password, config.ap.password, sizeof config.ap.password) &&
            actual.ap.authmode == WIFI_AUTH_WPA2_PSK && actual.ap.pairwise_cipher == WIFI_CIPHER_TYPE_CCMP;
        ESP_LOGI(TAG, "C6 AP readback verified=%d result=%s password_length=%u auth=%u cipher=%u",
            matches, esp_err_to_name(rc), (unsigned)strnlen((char *)actual.ap.password, sizeof actual.ap.password),
            actual.ap.authmode, actual.ap.pairwise_cipher);
        memset(&actual, 0, sizeof actual);
        memset(&config, 0, sizeof config);
        atomic_store(&ap_radio_ready, false);
        if (!matches || esp_wifi_start() != ESP_OK) return false;
        ap_started = true;
        for (unsigned i = 0; i < 100 && !atomic_load(&ap_radio_ready); ++i) vTaskDelay(pdMS_TO_TICKS(50));
        if (!atomic_load(&ap_radio_ready)) return false;
        portENTER_CRITICAL(&credentials_lock);
        memcpy(ap_ssid, done->ssid, sizeof ap_ssid);
        memcpy(ap_password, done->password, sizeof ap_password);
        portEXIT_CRITICAL(&credentials_lock);
        manual_ap = true;
        if (!saved) {
            work credentials = {0};
            memcpy(credentials.ssid, done->ssid, sizeof credentials.ssid);
            memcpy(credentials.password, done->password, sizeof credentials.password);
            if (!save_ap(&credentials)) { memset(&credentials,0,sizeof credentials); return false; }
            memset(&credentials,0,sizeof credentials);
        }
    } else {
        memcpy(done->ssid, ap_ssid, sizeof done->ssid);
        memcpy(done->password, ap_password, sizeof done->password);
    }
    if (udp >= 0) return true;
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp < 0) return false;
    struct timeval timeout = {.tv_usec = 10000};
    setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = INADDR_ANY};
    if (bind(udp, (struct sockaddr *)&local, sizeof local)) { close(udp); udp = -1; return false; }
    atomic_store(&receive_socket, udp);
    ESP_LOGI(TAG, "WIFI_READY WPA2 AP; UDP port %d (credentials omitted)", PORT);
    return true;
}
static void request_keyframe(void *context, uint32_t stream)
{
    (void)context; need_idr = true;
    atomic_store(&keyframe_stream, stream);
}
/* Baseline SOF0 only. Validate segment boundaries and exact visible geometry before the driver. */
static bool valid_jpeg(const uint8_t *p, size_t size)
{
    if (size < 4 || p[0] != 0xff || p[1] != 0xd8 || p[size-2] != 0xff || p[size-1] != 0xd9) return false;
    bool baseline = false;
    size_t at = 2;
    while (at + 4 <= size) {
        if (p[at++] != 0xff) return false;
        while (at < size && p[at] == 0xff) ++at;
        if (at + 3 > size) return false;
        unsigned marker = p[at++];
        unsigned len = ((unsigned)p[at] << 8) | p[at+1];
        if (len < 2 || len > size-at) return false;
        if (marker == 0xda) return baseline;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4) {
            if (marker != 0xc0 || len != 17 || p[at+2] != 8 || p[at+7] != 3 ||
                (((unsigned)p[at+3]<<8)|p[at+4]) != HEIGHT ||
                (((unsigned)p[at+5]<<8)|p[at+6]) != WIDTH) return false;
            baseline = true;
        }
        at += len;
    }
    return false;
}
/* JPEG DMA emits RGB565 most-significant byte first; LVGL uses native uint16_t. */
static void jpeg_copy_rgb565(uint16_t *target, const uint8_t *source, size_t pixels)
{
    for (size_t i = 0; i < pixels; ++i)
        target[i] = ((uint16_t)source[i * 2] << 8) | source[i * 2 + 1];
}
static bool jpeg_selftest(void)
{
    extern const uint8_t jpeg_start[] asm("_binary_jpeg_selftest_jpg_start");
    extern const uint8_t jpeg_end[] asm("_binary_jpeg_selftest_jpg_end");
    jpeg_decode_engine_cfg_t engine_config = {.timeout_ms = 100};
    jpeg_decode_memory_alloc_cfg_t memory = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    jpeg_pixels = jpeg_alloc_decoder_mem(WIDTH * 608 * 2, &memory, &jpeg_pixels_size);
    bool ok = jpeg_pixels && valid_jpeg(jpeg_start, jpeg_end-jpeg_start) &&
        jpeg_new_decoder_engine(&engine_config, &jpeg_decoder) == ESP_OK;
    uint32_t size = 0; uint64_t began = esp_timer_get_time();
    jpeg_decode_cfg_t config = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB};
    if (ok) ok = jpeg_decoder_process(jpeg_decoder, &config, jpeg_start, jpeg_end-jpeg_start,
        jpeg_pixels, jpeg_pixels_size, &size) == ESP_OK && size >= WIDTH*HEIGHT*2;
    uint16_t samples[4] = {0};
    const int expected[4][3] = {{30,5,2},{2,60,2},{2,5,30},{30,60,30}};
    if (ok) for (unsigned i=0;i<4;i++) {
        jpeg_copy_rgb565(&samples[i], jpeg_pixels + (300*WIDTH+i*256+128)*2, 1);
        int values[3] = {samples[i]>>11,(samples[i]>>5)&63,samples[i]&31};
        /* The peripheral's YUV conversion expands the source range slightly. */
        for(unsigned c=0;c<3;c++) if(abs(values[c]-expected[i][c])>6)ok=false;
    }
    ESP_LOGI(TAG, "JPEG_SELFTEST passed=%d decode_us=%llu output_bytes=%lu rgb565=%04x,%04x,%04x,%04x",
        ok, (unsigned long long)(esp_timer_get_time()-began), (unsigned long)size,
        samples[0],samples[1],samples[2],samples[3]);
    if(!ok) {
        if(jpeg_decoder)jpeg_del_decoder_engine(jpeg_decoder);
        jpeg_decoder=NULL;free(jpeg_pixels);jpeg_pixels=NULL;jpeg_pixels_size=0;
    }
    return ok;
}
static void jpeg_received(const moto_video_frame *frame)
{
    if (!jpeg_decoder || !valid_jpeg(frame->data, frame->size)) return;
    jpeg_decode_cfg_t config = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB};
    uint32_t size = 0;
    uint64_t started = esp_timer_get_time();
    esp_err_t result = jpeg_decoder_process(jpeg_decoder, &config, frame->data, frame->size,
        jpeg_pixels, jpeg_pixels_size, &size);
    decode_time_us += esp_timer_get_time()-started; ++decode_calls;
    if (result != ESP_OK || size < WIDTH*HEIGHT*2) return;
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    if (worker_epoch == atomic_load(&epoch) && terminal_video_active()) {
        /* The JPEG engine pads 600 to 608 lines; crop padding without scaling. */
        uint64_t converting = esp_timer_get_time();
        jpeg_copy_rgb565(rgb, jpeg_pixels, WIDTH*HEIGHT);
        convert_time_us += esp_timer_get_time() - converting; ++convert_calls;
        rgb_stream = frame->stream_id; rgb_frame = frame->frame_id; new_pixels = true; last_frame_valid = true;
        ++decoded_frames; atomic_store(&picture_time, (unsigned)now_ms()); atomic_store(&picture_ready, true);
    }
    xSemaphoreGive(pixels_mutex);
}
static void frame_received(void *context, const moto_video_frame *frame)
{
    (void)context;
    if (worker_epoch != atomic_load(&epoch) || !terminal_video_active()) return;
    if (video_codec == MOTO_VIDEO_MJPEG) { jpeg_received(frame); return; }
    if (!decoder) return;
    if (need_idr && !(frame->flags & 1)) return;
    esp_h264_dec_in_frame_t input = {
        .raw_data = {.buffer = (uint8_t *)frame->data, .len = frame->size},
        .pts = (uint32_t)frame->frame_id,
    };
    bool produced = false;
    while (input.raw_data.len) {
        esp_h264_dec_out_frame_t output = {0};
        input.consume = 0;
        uint64_t decode_start = esp_timer_get_time();
        esp_h264_err_t result = esp_h264_dec_process(decoder, &input, &output);
        decode_time_us += esp_timer_get_time() - decode_start; ++decode_calls;
        if (result != ESP_H264_ERR_OK || !input.consume || input.consume > input.raw_data.len) {
            request_keyframe(NULL, frame->stream_id); return;
        }
        input.raw_data.buffer += input.consume; input.raw_data.len -= input.consume;
        if (!output.out_size) continue;
        esp_h264_resolution_t resolution;
        if (esp_h264_dec_get_resolution(decoder_params, &resolution) != ESP_H264_ERR_OK ||
            resolution.width != WIDTH || (resolution.height != HEIGHT && resolution.height != 608) ||
            output.out_size < (unsigned)resolution.width * resolution.height * 3 / 2) {
            request_keyframe(NULL, frame->stream_id); return;
        }
        /* tinyH264 returns planar I420; a coded 608-line frame may carry a 600-line crop. */
        xSemaphoreTake(pixels_mutex, portMAX_DELAY);
        if (worker_epoch == atomic_load(&epoch) && terminal_video_active()) {
            uint64_t convert_start = esp_timer_get_time();
            video_color_convert(output.outbuf, resolution.height, rgb);
            convert_time_us += esp_timer_get_time() - convert_start; ++convert_calls;
            rgb_stream = frame->stream_id; rgb_frame = frame->frame_id; new_pixels = true; last_frame_valid = true;
            produced = true;
        }
        xSemaphoreGive(pixels_mutex);
    }
    if (produced) { atomic_store(&picture_time, (unsigned)now_ms()); atomic_store(&picture_ready, true); need_idr = false; ++decoded_frames; }
}
static bool prepare(const work *job)
{
    if (!ap_started || udp < 0 || job->stream.width != WIDTH || job->stream.height != HEIGHT) return false;
    if (receiver) { moto_video_destroy(receiver); receiver = NULL; }
    if (decoder) { esp_h264_dec_close(decoder); esp_h264_dec_del(decoder); decoder = NULL; }
    /* JPEG DMA resources are reserved once at boot. Stream teardown only clears
     * ownership/pixels; reallocating descriptor chains amid Wi-Fi traffic can fail. */
    video_codec = job->stream.codec;
    if (video_codec == MOTO_VIDEO_MJPEG) {
        if (!jpeg_pixels || !jpeg_decoder) return false;
    } else if (video_codec == MOTO_VIDEO_H264) {
        esp_h264_dec_cfg_sw_t config = {.pic_type = ESP_H264_RAW_FMT_I420};
        if (esp_h264_dec_sw_new(&config, &decoder) != ESP_H264_ERR_OK ||
            esp_h264_dec_open(decoder) != ESP_H264_ERR_OK ||
            esp_h264_dec_sw_get_param_hd(decoder, &decoder_params) != ESP_H264_ERR_OK) return false;
    } else return false;
    ESP_LOGI(TAG, "Decoder prepared codec=%s %ux%u fps=%lu bitrate=%lu (SDK STREAM_PREPARE)",
        video_codec == MOTO_VIDEO_MJPEG ? "MJPEG hardware" : "H264 software",
        (unsigned)job->stream.width, (unsigned)job->stream.height,
        (unsigned long)job->stream.fps, (unsigned long)job->stream.bitrate);
    moto_video_config video = {.frame = frame_received, .keyframe = request_keyframe,
        .max_frame_bytes = video_codec == MOTO_VIDEO_MJPEG ? 512 * 1024 : 256 * 1024, .slots = 4, .reorder_ms = 20, .assembly_ms = 120};
    receiver = moto_video_create(&video);
    received_packets = filtered_packets = 0; peer_probe_at = 0;
    decoded_frames = decode_time_us = decode_calls = 0;
    convert_time_us = convert_calls = 0;
    fps_at = now_ms(); fps_decoded = 0; fps_presented = atomic_load(&lcd_frames);
    atomic_store(&packet_overflows, 0);
    stream_id = job->stream.stream_id; peer_token = job->session; need_idr = true;
    return receiver != NULL;
}
static void receive_task(void *context)
{
    (void)context; packet incoming;
    for (;;) {
        int fd = atomic_load(&receive_socket);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        struct sockaddr_in sender; socklen_t size = sizeof sender;
        unsigned generation = atomic_load(&epoch);
        int count = recvfrom(fd, incoming.data, sizeof incoming.data, 0, (struct sockaddr *)&sender, &size);
        if (count <= 0 || count > 1400 || !terminal_video_active() || generation != atomic_load(&epoch)) continue;
        incoming.size = count; incoming.address = sender.sin_addr.s_addr;
        incoming.epoch = generation; incoming.arrived_ms = now_ms();
        if (xQueueSend(packets, &incoming, 0) != pdTRUE) atomic_fetch_add(&packet_overflows, 1);
    }
}
static void video_task(void *context)
{
    (void)context; packet incoming; uint64_t report_at = now_ms();
    completion boot = {0};
    {
        if (open_ap(&boot)) ESP_LOGI(TAG, "Persistent manual AP restored: %s", boot.ssid);
        else { manual_ap = false; cleanup(); ESP_LOGE(TAG, "Persistent manual AP start failed"); }
    }
    memset(&boot, 0, sizeof boot);
    for (;;) {
        unsigned latest = atomic_load(&epoch);
        if (latest != worker_epoch) { cleanup(); worker_epoch = latest; }
        work job;
        if (xQueueReceive(jobs, &job, udp < 0 ? pdMS_TO_TICKS(20) : 0) == pdTRUE) {
            if (job.manual_ap) {
                completion done = {0};
                if (job.ssid[0]) {
                    if (!save_ap(&job)) { memset(&job, 0, sizeof job); puts("MOTO_MANUAL_WIFI_SAVE_FAILED"); continue; }
                    manual_ap = false; cleanup();
                }
                memset(&job, 0, sizeof job);
                if (open_ap(&done)) {
                    manual_ap = true;
                    /* Only explicit local USB commands disclose AP credentials. */
                    printf("MOTO_MANUAL_WIFI {\"ssid\":\"%s\",\"password\":\"%s\",\"ip\":\"192.168.4.1\"}\n",
                           done.ssid, done.password);
                } else {
                    cleanup();
                    puts("MOTO_MANUAL_WIFI_FAILED");
                }
                memset(&done, 0, sizeof done);
                continue;
            }
            if (job.epoch != atomic_load(&epoch)) continue;
            completion done = {.job = job};
            if (job.kind == MOTO_EVENT_WIFI) {
                /* Repeated hotspot readiness requests preserve the video stream. */
                done.success = open_ap(&done);
                if (!done.success) cleanup();
                xQueueSend(completions, &done, 0); memset(&done, 0, sizeof done);
            } else if (job.kind == MOTO_EVENT_STREAM_PREPARE) {
                done.success = prepare(&job); xQueueSend(completions, &done, 0);
            } else if (job.kind == MOTO_EVENT_STREAM_START && receiver && peer_token == job.session && stream_id == job.stream.stream_id) {
                if (moto_video_begin_codec(receiver, peer_token, stream_id, WIDTH, HEIGHT, video_codec) == MOTO_OK) {
                    atomic_store(&preview_mode, job.stream.preview);
                    atomic_store(&playing_epoch, worker_epoch);
                    atomic_store(&playing, true);
                    ESP_LOGI(TAG, "STREAM_START id=%lu preview=%d", (unsigned long)stream_id, job.stream.preview);
                }
            }
        }
        if (udp < 0) continue;
        bool received = xQueueReceive(packets, &incoming, pdMS_TO_TICKS(10)) == pdTRUE;
        if (worker_epoch != atomic_load(&epoch)) continue;
        if (receiver && terminal_video_active()) {
            if (received && incoming.epoch == worker_epoch) {
                ++received_packets;
                if (accepted_peer(incoming.address)) moto_video_receive(receiver, peer_token, incoming.data, incoming.size, incoming.arrived_ms);
                else ++filtered_packets;
            }
            /* Queue delay during decode is not network reordering/packet loss. */
            if (!received) moto_video_tick(receiver, now_ms());
        }
        if (now_ms() - report_at >= 5000 && receiver) {
            moto_video_stats stats = moto_video_get_stats(receiver);
            ESP_LOGI(TAG, "VIDEO_STATS assembled=%llu decoded=%llu dropped=%llu recovered=%llu packets=%llu filtered=%llu",
                (unsigned long long)stats.frames, (unsigned long long)decoded_frames,
                (unsigned long long)stats.dropped, (unsigned long long)stats.recovered,
                (unsigned long long)received_packets, (unsigned long long)filtered_packets);
            ESP_LOGI(TAG, "DECODER_STATS avg_us=%llu convert_us=%llu worker_stack_free=%u",
                (unsigned long long)(decode_calls ? decode_time_us / decode_calls : 0),
                (unsigned long long)(convert_calls ? convert_time_us / convert_calls : 0),
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
            uint64_t measured_at = now_ms(), elapsed = measured_at - fps_at;
            unsigned shown = atomic_load(&lcd_frames);
            ESP_LOGI(TAG, "VIDEO_FPS decoded_x10=%llu displayed_x10=%llu queue=%u overflow=%u",
                (unsigned long long)(elapsed ? (decoded_frames - fps_decoded) * 10000 / elapsed : 0),
                (unsigned long long)(elapsed ? (uint64_t)(shown - fps_presented) * 10000 / elapsed : 0),
                (unsigned)uxQueueMessagesWaiting(packets), (unsigned)atomic_load(&packet_overflows));
            fps_at = measured_at; fps_decoded = decoded_frames; fps_presented = shown;
            report_at = now_ms();
        }
    }
}
bool terminal_video_init(void)
{
#if CONFIG_ESP_H264_DUAL_TASK
    ESP_LOGI(TAG, "H264 decoder dual-task mode with SIMD-capable buffers");
#else
    ESP_LOGI(TAG, "H264 decoder single-task mode");
#endif
    jobs = xQueueCreate(8, sizeof(work)); completions = xQueueCreate(8, sizeof(completion));
    pixels_mutex = xSemaphoreCreateMutex();
    uint8_t *packet_storage = heap_caps_malloc(RX_SLOTS * sizeof(packet), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (packet_storage) packets = xQueueCreateStatic(RX_SLOTS, sizeof(packet), packet_storage, &packet_queue);
    rgb = heap_caps_aligned_alloc(128, WIDTH * HEIGHT * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!jobs || !completions || !pixels_mutex || !rgb || !packets) return false;
    video_color_init(WIDTH, HEIGHT, rgb);
    esp_h264_dec_cfg_sw_t config = {.pic_type = ESP_H264_RAW_FMT_I420};
    esp_h264_dec_handle_t probe = NULL;
    bool ready = esp_h264_dec_sw_new(&config, &probe) == ESP_H264_ERR_OK;
    if (ready) ready = esp_h264_dec_open(probe) == ESP_H264_ERR_OK;
    if (probe) { esp_h264_dec_close(probe); esp_h264_dec_del(probe); }
    ready = jpeg_selftest() && ready;
    bool tasks_ready = xTaskCreatePinnedToCore(receive_task, "sdk_video_rx", 6144, NULL, 6, NULL, 0) == pdPASS &&
        xTaskCreatePinnedToCore(video_task, "sdk_video", 12288, NULL, 4, NULL, 0) == pdPASS;
    return ready && tasks_ready;
}
bool terminal_video_manual_ap(void)
{
    work job = {.manual_ap = true};
    return jobs && xQueueSend(jobs, &job, 0) == pdTRUE;
}
bool terminal_video_configure_ap(const char *ssid, const char *password)
{
    if (!valid_ap(ssid, password)) return false;
    work job = {.manual_ap = true};
    memcpy(job.ssid, ssid, strlen(ssid)); memcpy(job.password, password, strlen(password));
    bool ok = jobs && xQueueSend(jobs, &job, 0) == pdTRUE;
    memset(&job, 0, sizeof job); return ok;
}
void terminal_video_event(const moto_event *event)
{
    if (!jobs) return;
    switch (event->kind) {
    case MOTO_EVENT_CLOSED: case MOTO_EVENT_UNPAIR: case MOTO_EVENT_NAV_END:
    case MOTO_EVENT_WIFI_STOP: case MOTO_EVENT_STREAM_STOP:
        atomic_store(&playing, false); atomic_fetch_add(&epoch, 1); break;
    case MOTO_EVENT_WIFI: case MOTO_EVENT_STREAM_PREPARE: case MOTO_EVENT_STREAM_START: {
        work job = {.epoch = atomic_load(&epoch), .kind = event->kind,
            .session = event->session_id, .request = event->request_id};
        if (event->stream) job.stream = *event->stream;
        if (xQueueSend(jobs, &job, 0) != pdTRUE) {
            atomic_store(&playing, false); atomic_fetch_add(&epoch, 1);
        }
        break;
    }
    default: break;
    }
}
void terminal_video_poll(moto_sdk *sdk)
{
    if (!completions) return;
    completion done;
    while (xQueueReceive(completions, &done, 0) == pdTRUE) {
        if (done.job.epoch == atomic_load(&epoch) && done.job.session == moto_session_id(sdk) && moto_authenticated(sdk)) {
            moto_result rc;
            if (done.job.kind == MOTO_EVENT_WIFI) rc = done.success ?
                moto_wifi_complete(sdk, done.job.session, done.job.request, done.ssid, done.password, "192.168.4.1", PORT) :
                moto_wifi_failed(sdk, done.job.session, done.job.request, (uint32_t)-MOTO_IO);
            else rc = moto_stream_prepared(sdk, done.job.session, done.job.request, done.success);
            if (rc != MOTO_OK) { atomic_store(&playing, false); atomic_fetch_add(&epoch, 1); }
        }
        memset(&done, 0, sizeof done);
    }
    uint32_t requested = atomic_exchange(&keyframe_stream, 0);
    if (requested && terminal_video_active()) moto_request_keyframe(sdk, requested);
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    bool report = new_presented && presented_epoch == atomic_load(&epoch); uint32_t stream = presented_stream; uint64_t frame = presented_frame;
    new_presented = false;
    xSemaphoreGive(pixels_mutex);
    if (report && terminal_video_active()) moto_report_presented(sdk, stream, frame);
}
bool terminal_video_active(void) { return atomic_load(&playing) && atomic_load(&playing_epoch) == atomic_load(&epoch); }
bool terminal_video_visible(void) { return terminal_video_active() && atomic_load(&picture_ready) && (unsigned)((unsigned)now_ms()-atomic_load(&picture_time)) < 2500; }
bool terminal_video_copy_current(uint16_t *pixels)
{
    if (!pixels_mutex || !terminal_video_visible()) return false;
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    bool ready = terminal_video_visible();
    if (ready) memcpy(pixels, rgb, WIDTH * HEIGHT * 2);
    xSemaphoreGive(pixels_mutex);
    return ready;
}
bool terminal_video_copy(uint16_t *pixels, uint32_t *stream, uint64_t *frame, unsigned *generation)
{
    if (!pixels_mutex || !terminal_video_active()) return false;
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    bool ready = new_pixels && terminal_video_visible();
    if (ready) {
        memcpy(pixels, rgb, WIDTH * HEIGHT * 2); *stream = rgb_stream; *frame = rgb_frame; *generation = atomic_load(&playing_epoch);
        new_pixels = false;
    }
    xSemaphoreGive(pixels_mutex); return ready;
}
void terminal_video_presented(uint32_t stream, uint64_t frame, unsigned generation)
{
    if (generation == atomic_load(&epoch) && terminal_video_active()) atomic_fetch_add(&lcd_frames, 1);
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    presented_stream = stream; presented_frame = frame; presented_epoch = generation; new_presented = true;
    xSemaphoreGive(pixels_mutex);
}

bool terminal_video_preview(void) { return terminal_video_active() && atomic_load(&preview_mode); }
bool terminal_video_copy_map(uint16_t *pixels, bool only_new, uint32_t *stream,
                             uint64_t *frame, unsigned *generation)
{
    if (!pixels_mutex || !rgb) return false;
    xSemaphoreTake(pixels_mutex, portMAX_DELAY);
    bool live=terminal_video_preview() && terminal_video_visible();
    bool ready=last_frame_valid && (!only_new || (live && new_pixels));
    if(ready) {
        static bool reported_frozen;
        if(!live && !reported_frozen)
            ESP_LOGI(TAG,"Home retained last frame stream=%lu frame=%llu",(unsigned long)rgb_stream,(unsigned long long)rgb_frame);
        reported_frozen=!live;
        /* Center-crop native pixels: street labels never get downsampled. */
        cockpit_map_copy(pixels,rgb,WIDTH,COCKPIT_MAP_CENTER_X-WIDTH/2,COCKPIT_MAP_CENTER_Y-HEIGHT/2);
        if(stream && frame && generation) {
            /* Frozen frames repaint the home page without emitting a new receipt. */
            *stream=live?rgb_stream:0; *frame=rgb_frame; *generation=live?atomic_load(&playing_epoch):0;
            if(live)new_pixels=false;
        }
    }
    xSemaphoreGive(pixels_mutex);
    return ready;
}
