#include "route_map_store.h"
#include "terminal_board.h"
#include "terminal_video.h"
#include "ancs_adapter.h"
/* moto-sdk BLE/TLS reference adapter, with JC1060 P4/C6 board hooks. */
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_hosted.h"
#include "terminal_config.h"
#include "factory_usb.h"
#include "factory_license.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/md.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "moto/sdk.h"
#include "moto/tls.h"



void ble_store_config_init(void);

static const char *TAG = "MotoSdk";
static atomic_bool sdk_ready;
static atomic_bool decoder_ready;
static atomic_bool hci_ready;
bool moto_terminal_ble_ready(void) { return atomic_load(&hci_ready); }
bool moto_terminal_video_ready(void) { return atomic_load(&decoder_ready); }
static moto_sdk *sdk;
static moto_tls *tls;
static nvs_handle_t storage;
static char identity[65], device_id[65], model_code[65], discovery_id[17];
static uint8_t key[32], ble_mac[6], cloud_proof_key[32];
static bool cloud_configured;
static bool commissioned;
typedef struct {
    moto_sdk *sdk;
    moto_tls *tls;
    uint16_t conn;
    uint64_t generation, connected_at, close_at, session_started;
    bool subscribed, disconnect_requested, candidate;
    uint8_t tx_ring[8192];
    size_t tx_head, tx_count;
} ble_peer;
static ble_peer peers[2];
static ble_peer *active;
static moto_config base_config;
static uint16_t tx_handle;
static uint64_t next_generation;
static bool create_peer(ble_peer *peer);
static struct ble_npl_callout timer;
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x87,0x65,0x19,0x81,0x11,0x94,0xa0,0x9c,0x5e,0x4f,0x39,0x52,0x01,0,0x4d,0x6d);
static const ble_uuid128_t rx_uuid = BLE_UUID128_INIT(0x87,0x65,0x19,0x81,0x11,0x94,0xa0,0x9c,0x5e,0x4f,0x39,0x52,0x02,0,0x4d,0x6d);
static const ble_uuid128_t tx_uuid = BLE_UUID128_INIT(0x87,0x65,0x19,0x81,0x11,0x94,0xa0,0x9c,0x5e,0x4f,0x39,0x52,0x03,0,0x4d,0x6d);
static void advertise(void);
static int gap_event(struct ble_gap_event *, void *);

static uint64_t clock_ms(void *user) { (void)user; return esp_timer_get_time() / 1000; }
static int random_bytes(void *user, uint8_t *bytes, size_t size) { (void)user; esp_fill_random(bytes, size); return 0; }
static int send_cipher(void *user, const uint8_t *bytes, size_t size) {
    ble_peer *peer = user;
    if (peer->conn == BLE_HS_CONN_HANDLE_NONE) return -1;
    if (size > sizeof peer->tx_ring - peer->tx_count) size = sizeof peer->tx_ring - peer->tx_count;
    for (size_t i = 0; i < size; ++i)
        peer->tx_ring[(peer->tx_head + peer->tx_count + i) % sizeof peer->tx_ring] = bytes[i];
    peer->tx_count += size;
    return (int)size;
}
static int persist_pairing(void *user, bool paired) {
    (void)user;
    esp_err_t err = paired ? ESP_OK : factory_license_clear_owner();
    if (err == ESP_OK) commissioned = paired;
    return err == ESP_OK ? 0 : -1;
}
/* Vehicle state remains a fixture; local audio/LED execute on a separate task. */
static moto_vehicle_state simulated_state;
static uint64_t state_revision, next_sample, fault_event_id, next_fault, session_started_ms;
static control_effect rejected_commands[16];
static unsigned rejected_count;
static void complete_effect(const control_effect *effect) {
    if (effect->success) {
        unsigned field = MOTO_LOCK + effect->message - MOTO_LOCK_SET;
        if (field != MOTO_HORN && !(field == MOTO_LIGHT && effect->value == 2))
            simulated_state.values[field] = effect->value;
        next_sample = 0;
    }
    if (effect->session != moto_session_id(sdk) || !moto_authenticated(sdk)) return;
    moto_result result = moto_complete_command(sdk, effect->session, effect->request,
        effect->success ? MOTO_SUCCEEDED : MOTO_FAILED, effect->success ? 0 : (uint32_t)-MOTO_IO);
    ESP_LOGI(TAG, "CONTROL_RESULT type=%" PRIu32 " value=%" PRIu32 " success=%d result=%d",
             effect->message, effect->value, effect->success, result);
}
static void simulate_vehicle(void) {
    uint64_t now = clock_ms(NULL);
    for (unsigned i=0; i<rejected_count; ++i) complete_effect(&rejected_commands[i]);
    rejected_count=0;
    control_effect effect;
    while(control_effects_result(&effect)) complete_effect(&effect);
    if (now >= next_sample) {
        next_sample = now + 5000;
        /* One pure EV fixture for the instrument and authenticated App reports. */
        electric_state_t demo = electric_demo_sample((uint32_t)now);
        simulated_state.values[MOTO_BATTERY] = demo.battery_percent;
        simulated_state.values[MOTO_CHARGING] = demo.charging ? 2 : 1;
        simulated_state.values[MOTO_RANGE_METRES] = demo.range_km * 1000;
        simulated_state.values[MOTO_MILEAGE] = 1286000 + demo.trip_tenths * 100;
        simulated_state.values[MOTO_FAULT] = (now / 60000) % 2; /* active simulated fault count */
        uint32_t enabled=moto_enabled_capabilities(sdk);
        simulated_state.present = enabled & (MOTO_CAP(MOTO_LOCK)|MOTO_CAP(MOTO_VOLUME)|MOTO_CAP(MOTO_LIGHT)|MOTO_CAP(MOTO_BATTERY)|MOTO_CAP(MOTO_CHARGING)|MOTO_CAP(MOTO_MILEAGE)|MOTO_CAP(MOTO_FAULT));
        if(enabled & MOTO_CAP(MOTO_BATTERY))simulated_state.present|=MOTO_CAP(MOTO_RANGE_METRES);
        moto_result result = moto_report_vehicle_state(sdk, ++state_revision, &simulated_state);
        ESP_LOGI(TAG, "SIM_STATE revision=%" PRIu64 " soc=%" PRIu64 " range_m=%" PRIu64 " result=%d",
                 state_revision, simulated_state.values[MOTO_BATTERY], simulated_state.values[MOTO_RANGE_METRES], result);
    }
    if (moto_authenticated(sdk) && now >= next_fault) {
        bool active = (now / 60000) % 2;
        moto_result result = moto_report_fault(sdk, ++fault_event_id, active, "moto.simulator", 1001, 1);
        next_fault = now + (result == MOTO_OK ? 15000 : 1000);
    }
}
static int persist_auth(void *user, const moto_pairing_auth *auth) {
    (void)user;
    uint8_t record[67] = {2, auth->has_owner ? 1 : 0, auth->failed_attempts};
    memcpy(record + 3, auth->owner_token, 32);
    memcpy(record + 35, auth->released_challenge, 32);
    esp_err_t result = nvs_set_blob(storage, "license_auth", record, sizeof record);
    if (result == ESP_OK) result = nvs_commit(storage);
    memset(record, 0, sizeof record);
    return result == ESP_OK ? 0 : -1;
}
static bool load_auth(moto_pairing_auth *auth) {
    uint8_t record[67] = {0}; size_t size = sizeof record;
    esp_err_t result = nvs_get_blob(storage, "license_auth", record, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) { memset(auth, 0, sizeof *auth); return true; }
    if (result != ESP_OK || ((size != 35 || record[0] != 1) && (size != 67 || record[0] != 2)) || record[1] > 1 || record[2] > 5) return false;
    memset(auth, 0, sizeof *auth);
    if (size == 67) memcpy(auth->released_challenge, record + 35, 32);
    auth->has_owner = record[1]; auth->failed_attempts = record[2];
    memcpy(auth->owner_token, record + 3, 32); memset(record, 0, sizeof record); return true;
}
static int cloud_hmac(void *user, const uint8_t *data, size_t size, uint8_t out[32]) {
    (void)user;
    if (!cloud_configured || size > 1057) return -1;
    return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), cloud_proof_key, 32, data, size, out);
}
static void on_event(void *user, const moto_event *event) {
    ble_peer *peer = user;
    if (event->kind == MOTO_EVENT_CLOSED) {
        peer->disconnect_requested = true;
        peer->close_at = clock_ms(NULL) + 150;
    }
    if (event->kind == MOTO_EVENT_READY) peer->session_started = clock_ms(NULL);
    if (peer != active) return; /* Candidates never touch vehicle/UI/ANCS/OTA adapters. */
    hud_display_event(event);
    terminal_map_event(event);
    terminal_video_event(event);
    switch (event->kind) {
    case MOTO_EVENT_PAIRING_PIN:
        ESP_LOGI(TAG, "PAIR_PIN_DISPLAYED expires_s=120 (PIN not logged)"); break;
    case MOTO_EVENT_AUTHENTICATED:
        ESP_LOGI(TAG, "OWNER_AUTHENTICATED"); break;
    case MOTO_EVENT_VEHICLE_COMMAND: {
        /* No SDK reentry: worker executes, host tick delivers the final result. */
        moto_slice field; uint64_t expiry=0;
        if(moto_cbor_find(event->body,2,&field)) moto_cbor_get_uint(field,&expiry);
        control_effect effect={.session=event->session_id,.request=event->request_id,
            .message=event->message,.value=event->value,.deadline_ms=session_started_ms+expiry};
        if(!control_effects_submit(effect) && rejected_count<16)
            rejected_commands[rejected_count++]=effect;
        break;
    }
    case MOTO_EVENT_PHONE_STATE:
        ESP_LOGI(TAG, "PHONE_STATE revision=%" PRIu64 " theme=%d battery=%d charging=%d",
                 event->phone->revision, event->phone->theme, event->phone->battery_percent,
                 event->phone->charging); break;
    case MOTO_EVENT_ANCS_SHARING:
        terminal_ancs_request(event->value == 1); break;
    case MOTO_EVENT_READY:
        session_started_ms = clock_ms(NULL);
        ESP_LOGI(TAG, "TLS_READY cipher=%s", moto_tls_ciphersuite(tls)); break;
    case MOTO_EVENT_PAIR:
        ESP_LOGI(TAG, "PAIR_COMMITTED device=%s hud=semantic", device_id); break;
    case MOTO_EVENT_UNPAIR:
        terminal_ancs_disconnect();
        ESP_LOGI(TAG, "UNPAIRED owner revoked; immutable License retained"); break;
    case MOTO_EVENT_CLOSED:
        terminal_ancs_disconnect();
        control_effects_cancel();
        break;
    case MOTO_EVENT_NAV_BEGIN: {
        moto_slice field; uint64_t source = 0;
        if (moto_cbor_find(event->body, 1, &field)) moto_cbor_get_uint(field, &source);
        ESP_LOGI(TAG, "NAV_BEGIN source=%s", source == 1 ? "mirror" : "navigation");
        break;
    }
    case MOTO_EVENT_NAV_END:
        ESP_LOGI(TAG, "NAV_END"); break;
    case MOTO_EVENT_HUD:
        ESP_LOGI(TAG, "HUD revision=%" PRIu64 " action=%" PRIu32 " distance_m=%" PRIu32 " remain_m=%" PRIu32 " remain_s=%" PRIu32 " road_bytes=%u next_bytes=%u",
                 event->hud->revision, event->hud->action, event->hud->distance_m,
                 event->hud->remaining_m, event->hud->remaining_s,
                 (unsigned)strlen(event->hud->road), (unsigned)strlen(event->hud->next_road)); break;
    case MOTO_EVENT_HUD_EXPIRED:
        ESP_LOGI(TAG, "HUD_EXPIRED"); break;
    case MOTO_EVENT_NOTIFICATION: {
        moto_slice value; uint64_t id = 0;
        if (moto_cbor_find(event->body, 0, &value)) moto_cbor_get_uint(value, &id);
        ESP_LOGI(TAG, "NOTIFICATION id=%" PRIu64 " bytes=%u (content not logged)", id, (unsigned)event->body.size); break;
    }
    case MOTO_EVENT_NOTIFICATION_REMOVE:
        ESP_LOGI(TAG, "NOTIFICATION_REMOVED"); break;
    default: break;
    }
}
static ble_peer *peer_for(uint16_t connection) {
    for (unsigned i = 0; i < 2; ++i)
        if (peers[i].conn == connection) return &peers[i];
    return NULL;
}
static void promote_peer(ble_peer *peer) {
    if (!peer->candidate || !moto_handoff_pending(peer->sdk)) return;
    if (moto_handoff_promote(peer->sdk) != MOTO_OK) return;

    terminal_ancs_disconnect();
    control_effects_cancel();
    active = peer; sdk = peer->sdk; tls = peer->tls;
    peer->candidate = false;
    terminal_ancs_connect(peer->conn);
    session_started_ms = peer->session_started;
    next_sample = next_fault = 0;

    ESP_LOGI(TAG, "PHONE_HANDOFF_AUTHENTICATED generation=%" PRIu64, peer->generation);
}
static int access_gatt(uint16_t connection, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr; (void)arg;
    ble_peer *peer = peer_for(connection);
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !peer || !peer->subscribed) return BLE_ATT_ERR_UNLIKELY;
    uint8_t bytes[512]; uint16_t size;
    if (ble_hs_mbuf_to_flat(ctxt->om, bytes, sizeof bytes, &size)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    moto_result result = moto_receive(peer->sdk, peer->generation, bytes, size);
    if (result != MOTO_OK) {
        peer->disconnect_requested = true;
        ESP_LOGW(TAG, "RX_FAILED code=%d", result);
        return BLE_ATT_ERR_UNLIKELY;
    }
    promote_peer(peer);
    return 0;
}
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid=&rx_uuid.u, .access_cb=access_gatt, .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
        {.uuid=&tx_uuid.u, .access_cb=access_gatt, .flags=BLE_GATT_CHR_F_NOTIFY, .val_handle=&tx_handle},
        {0}
    }
}, {0}};
static void flush_cipher(ble_peer *peer) {
    if (!peer->subscribed || peer->conn == BLE_HS_CONN_HANDLE_NONE) return;
    for (unsigned budget = 0; peer->tx_count && budget < 4; ++budget) {
        size_t size = ble_att_mtu(peer->conn) - 3;
        if (size > peer->tx_count) size = peer->tx_count;
        if (size > 244) size = 244;
        uint8_t bytes[244];
        for (size_t i = 0; i < size; ++i) bytes[i] = peer->tx_ring[(peer->tx_head + i) % sizeof peer->tx_ring];
        struct os_mbuf *buffer = ble_hs_mbuf_from_flat(bytes, size);
        if (!buffer) return;
        int result = ble_gatts_notify_custom(peer->conn, tx_handle, buffer);
        if (result == BLE_HS_ENOMEM || result == BLE_HS_EBUSY || result == BLE_HS_EAGAIN) return;
        if (result) { peer->disconnect_requested = true; return; }
        peer->tx_head = (peer->tx_head + size) % sizeof peer->tx_ring; peer->tx_count -= size;
    }
}
static void on_tick(struct ble_npl_event *event) {
    (void)event;
    if (!atomic_load(&sdk_ready)) {
        ble_npl_callout_reset(&timer, ble_npl_time_ms_to_ticks32(20));
        return;
    }
    for (unsigned i = 0; i < 2; ++i) {
        ble_peer *peer = &peers[i];
        if (peer->conn == BLE_HS_CONN_HANDLE_NONE) continue;
        if (peer->subscribed) {
            flush_cipher(peer);
            if (!peer->disconnect_requested) moto_poll(peer->sdk);
            promote_peer(peer);
            flush_cipher(peer);
        } else if (clock_ms(NULL) - peer->connected_at >= 20000) peer->disconnect_requested = true;
        if (peer->disconnect_requested && clock_ms(NULL) >= peer->close_at)
            ble_gap_terminate(peer->conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    terminal_ancs_tick(moto_authenticated(sdk));
    if (terminal_ancs_should_disconnect()) active->disconnect_requested = true;
    terminal_video_poll(sdk);
    terminal_map_poll(sdk);
    simulate_vehicle();
    advertise();
    ble_npl_callout_reset(&timer, ble_npl_time_ms_to_ticks32(20));
}
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (!atomic_load(&sdk_ready)) return 0;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status) { advertise(); break; }
        ble_peer *peer = active->conn == BLE_HS_CONN_HANDLE_NONE ? active : NULL;
        if (!peer) for (unsigned i = 0; i < 2; ++i)
            if (&peers[i] != active && peers[i].conn == BLE_HS_CONN_HANDLE_NONE) peer = &peers[i];
        if (!peer || (!peer->sdk && !create_peer(peer))) {
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_CONN_LIMIT); break;
        }
        peer->conn = event->connect.conn_handle; peer->generation = ++next_generation;
        peer->connected_at = clock_ms(NULL); peer->close_at = 0;
        peer->candidate = peer != active;
        if (peer == active) terminal_ancs_connect(peer->conn);
        peer->subscribed = false; peer->tx_head = peer->tx_count = 0; peer->disconnect_requested = false;
                ESP_LOGI(TAG, "BLE_CONNECTED generation=%" PRIu64 " candidate=%d", peer->generation, peer->candidate);
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        ble_peer *peer = peer_for(event->disconnect.conn.conn_handle);
        if (!peer) break;
        if (peer == active) { terminal_ancs_disconnect(); control_effects_cancel(); }
        moto_disconnect(peer->sdk, peer->generation);
        peer->conn = BLE_HS_CONN_HANDLE_NONE; peer->subscribed = false; peer->disconnect_requested = false;
        peer->tx_head = peer->tx_count = 0; memset(peer->tx_ring, 0, sizeof peer->tx_ring);
        if (peer != active) { moto_destroy(peer->sdk); moto_tls_destroy(peer->tls); peer->sdk = NULL; peer->tls = NULL; }
        ESP_LOGI(TAG, "BLE_DISCONNECTED reason=%d", event->disconnect.reason);
        advertise(); break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE: {
        ble_peer *peer = peer_for(event->subscribe.conn_handle);
        if (!peer || event->subscribe.attr_handle != tx_handle) break;
        if (!event->subscribe.cur_notify) { peer->disconnect_requested = true; break; }
        if (!peer->subscribed) {
            peer->subscribed = true;
            moto_result rc = peer->candidate ? moto_connect_candidate(peer->sdk, sdk, peer->generation)
                                            : moto_connect(peer->sdk, peer->generation);
            if (rc != MOTO_OK) peer->disconnect_requested = true;
        }
        break;
    }
    default: break;
    }
    return terminal_ancs_gap(event, moto_authenticated(sdk));
}
static void advertise(void) {
    if (!atomic_load(&sdk_ready) || !ble_hs_synced()) return;
    if (ble_gap_adv_active()) return;
    if (peers[0].conn != BLE_HS_CONN_HANDLE_NONE && peers[1].conn != BLE_HS_CONN_HANDLE_NONE) return;
    if (active->conn != BLE_HS_CONN_HANDLE_NONE && !moto_has_owner(sdk)) return;
    uint8_t data[31] = {2, BLE_HS_ADV_TYPE_FLAGS, BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP};
    size_t size;
    if (moto_advertising(sdk, data + 3, sizeof data - 3, &size) != MOTO_OK) return;
    bool discoverable = strlen(discovery_id)==16;
    size_t adv_size=size+3;
    if (discoverable && adv_size+8<=sizeof data) {
        data[adv_size++]=7; data[adv_size++]=BLE_HS_ADV_TYPE_COMP_NAME;
        memcpy(data+adv_size,"MotoP4",6); adv_size+=6;
    }
    int rc = ble_gap_adv_set_data(data, adv_size);
    if (rc) { ESP_LOGE(TAG, "ADV_FAILED code=%d", rc); return; }
    if (discoverable) {
        /* 28 bytes: SDK service data plus an opaque 64-bit public discovery reference.
           No permanent identity or secret is included. Fits legacy scan response. */
        uint8_t response[28]={27,0x21};
        memcpy(response+2, service_uuid.value,16); response[18]=1; response[19]=1;
        for(unsigned i=0;i<8;i++){unsigned byte=0;sscanf(discovery_id+i*2,"%2x",&byte);response[20+i]=(uint8_t)byte;}
        rc=ble_gap_adv_rsp_set_data(response,sizeof response);
    } else {
        struct ble_hs_adv_fields response = {0};
        response.uuids128 = (ble_uuid128_t *)&service_uuid;
        response.num_uuids128 = 1; response.uuids128_is_complete = 1;
        response.name = (uint8_t *)"MotoP4-SDK"; response.name_len = 10; response.name_is_complete = 1;
        rc = ble_gap_adv_rsp_set_fields(&response);
    }
    if (rc) { ESP_LOGE(TAG, "SCAN_RESPONSE_FAILED code=%d", rc); return; }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 160; params.itvl_max = 240;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    ESP_LOGI(TAG, "READY name=MotoP4-SDK device=%s result=%d", device_id, rc);
}
static void on_sync(void) {
    /* NimBLE persists Service Changed for bonded peers, including offline ones.
     * Bump only when the GATT database changes; do not erase bonds or device keys. */
    uint32_t revision = 0;
    if (nvs_get_u32(storage, "gatt_revision", &revision) != ESP_OK || revision != 2) {
        ble_svc_gatt_changed(1, UINT16_MAX);
        if (nvs_set_u32(storage, "gatt_revision", 2) == ESP_OK)
            ESP_ERROR_CHECK(nvs_commit(storage));
    }
    ESP_ERROR_CHECK(ble_hs_id_set_rnd(ble_mac));
    atomic_store(&hci_ready, true);
    ESP_LOGI(TAG, "BLE_HCI_READY P4 NimBLE host synchronized with C6 controller");
    advertise();
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
static void on_host_reset(int reason) {
    atomic_store(&hci_ready, false);
    ESP_LOGW(TAG, "BLE host reset reason=%d", reason);
}

static bool load_credentials(void) {
    moto_license license;
    if(factory_license_load(&license))return false;
    snprintf(device_id,sizeof device_id,"%s",license.terminal_id);
    snprintf(identity,sizeof identity,"%s",license.identity);
    snprintf(model_code,sizeof model_code,"%s",license.model);
    snprintf(discovery_id,sizeof discovery_id,"%s",license.discovery_id);
    memcpy(key,license.psk,32);memcpy(cloud_proof_key,license.proof_key,32);
    cloud_configured=true;commissioned=false;
    moto_license_clear(&license);return true;
}
static bool create_peer(ble_peer *peer) {
    /* License was verified once at boot. Avoid signature verification and NVS work
       on every radio connection; the device-scoped PSK remains private to this adapter. */
    moto_tls_config transport = {peer, random_bytes, send_cipher, key, sizeof key, identity, false};
    peer->tls = moto_tls_create(&transport);
    if (!peer->tls) return false;
    moto_config cfg = base_config;
    cfg.user = peer; cfg.secure = moto_tls_transport(peer->tls);
    if (!load_auth(&cfg.pairing_auth)) { moto_tls_destroy(peer->tls); peer->tls = NULL; return false; }
    peer->sdk = moto_create(&cfg);
    if (!peer->sdk) { moto_tls_destroy(peer->tls); peer->tls = NULL; return false; }
    return true;
}
static void start_ble_host(void) {
    terminal_ancs_init();
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_host_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_sc = 1; ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_svc_gap_init(); ble_svc_gatt_init();
    assert(ble_gatts_count_cfg(services) == 0); assert(ble_gatts_add_svcs(services) == 0);
    assert(ble_svc_gap_device_name_set("MotoP4-SDK") == 0);
    ble_npl_callout_init(&timer, nimble_port_get_dflt_eventq(), on_tick, NULL);
    ble_npl_callout_reset(&timer, ble_npl_time_ms_to_ticks32(20));
    ESP_LOGI(TAG, "BOOT SDK 0.1 P4 BLE/Wi-Fi/video adapter; free_heap=%u", (unsigned)esp_get_free_heap_size());
    nimble_port_freertos_init(host_task);
}
void p4_terminal_start(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("moto_sdk", NVS_READWRITE, &storage));
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    snprintf(device_id, sizeof device_id, "moto-p4-%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    factory_license_init(storage, device_id);
#ifdef CONFIG_MOTO_FACTORY_USB
    factory_usb_start();
#endif
    control_effects_init();
    esp_hosted_connect_to_slave();
    esp_hosted_coprocessor_fwver_t fwver = {0};
    if (esp_hosted_get_coprocessor_fwversion(&fwver) == ESP_OK)
        ESP_LOGI(TAG, "C6 firmware %lu.%lu.%lu", (unsigned long)fwver.major1,
                 (unsigned long)fwver.minor1, (unsigned long)fwver.patch1);
    /* ESP-Hosted 2.5.2 introduced manual controller startup. Factory 2.3.2
       already boots it; issuing the new RPC there returns NOT_SUPPORTED. */
    if (fwver.major1 > 2 || (fwver.major1 == 2 &&
        (fwver.minor1 > 5 || (fwver.minor1 == 5 && fwver.patch1 >= 2)))) {
        ESP_ERROR_CHECK(esp_hosted_bt_controller_init());
        ESP_ERROR_CHECK(esp_hosted_bt_controller_enable());
    }
    for (unsigned i = 0; i < 6; ++i) ble_mac[i] = mac[5 - i];
    ble_mac[5] |= 0xc0;
    start_ble_host();
    bool video_ready = terminal_video_init();
    atomic_store(&decoder_ready, video_ready);
    ESP_LOGI(TAG, "BOARD_ADAPTERS_READY video=%d; awaiting device License", video_ready);
    if (!load_credentials()) {
        hud_display_set_provisioning(true);
        while (!load_credentials()) vTaskDelay(pdMS_TO_TICKS(200));
    }
    hud_display_set_provisioning(false);
    moto_config cfg = {0};
    cfg.now_ms = clock_ms; cfg.random = random_bytes; cfg.event = on_event; cfg.persist_pairing = persist_pairing;
    cfg.device_id = device_id; cfg.commissioned = commissioned;
    cfg.cloud_hmac = cloud_configured ? cloud_hmac : NULL;
    cfg.device_model_code = model_code;
    cfg.hardware_revision = "JC1060-ESP32-P4-C6";
    cfg.firmware_version = esp_app_get_description()->version;
    cfg.phone_state = true;
    cfg.map = terminal_map_port();
    cfg.route_navigation = cfg.map.max_bytes != 0;
    cfg.instrument = terminal_board_instrument_port();
    cfg.pairing_pin = true; cfg.persist_pairing_auth = persist_auth;
    assert(load_auth(&cfg.pairing_auth));
    /* The backend supplies all model capabilities and operation limits after owner authentication. */
    terminal_configure_board(&cfg, video_ready);
    simulated_state.values[MOTO_VOLUME] = 50;
    cfg.ancs = true; cfg.ancs_ready = terminal_ancs_ready;
    cfg.notifications = true; cfg.calls = false; cfg.wifi_ota = false;
    base_config = cfg;
    peers[0].conn = peers[1].conn = BLE_HS_CONN_HANDLE_NONE;
    active = &peers[0]; assert(create_peer(active));
    sdk = active->sdk; tls = active->tls;
    atomic_store(&sdk_ready, true);
}
