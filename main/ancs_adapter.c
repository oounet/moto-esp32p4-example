/* NimBLE transport for moto-sdk's ANCS parser. Single host-task owner. */
#include "ancs_adapter.h"
#include "hud_display.h"
#include "moto/ancs.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <inttypes.h>
#include <assert.h>
static const char *TAG = "MotoANCS";
static const ble_uuid128_t ancs_service = BLE_UUID128_INIT(
    0xd0,0,0x2d,0x12,0x1e,0x4b,0x0f,0xa4,0x99,0x4e,0xce,0xb5,0x31,0xf4,5,0x79);
enum { DATA, SOURCE, CONTROL, CHANGED, CHAR_COUNT };
static const ble_uuid128_t ancs_chars[] = {
    BLE_UUID128_INIT(0xfb,0x7b,0x7c,0xce,0x6a,0xb3,0x44,0xbe,0xb5,0x4b,0xd6,0x24,0xe9,0xc6,0xea,0x22),
    BLE_UUID128_INIT(0xbd,0x1d,0xa2,0x99,0xe6,0x25,0x58,0x8c,0xd9,0x42,1,0x63,0x0d,0x12,0xbf,0x9f),
    BLE_UUID128_INIT(0xd9,0xd9,0xaa,0xfd,0xbd,0x9b,0x21,0x98,0xa8,0x49,0xe1,0x45,0xf3,0xd8,0xd1,0x69),
};


typedef struct { uint16_t value, end, cccd; } remote_char_t;
typedef struct {
    uint16_t conn;
    uintptr_t epoch;
    uint64_t ancs_epoch;
    bool want_ancs, encrypted, security_started, ready, busy, disconnect, bond_repaired;
    int64_t deadline, retry_at;
    uint16_t ancs_start, ancs_end, gatt_start, gatt_end;
    remote_char_t chars[CHAR_COUNT];
    int last_char, descriptor_index, subscribe_index;
    bool discovering_gatt;
    unsigned early_count;
    uint8_t early[16][8];
} session_t;
static session_t session = {.conn = BLE_HS_CONN_HANDLE_NONE};
static uintptr_t next_epoch;
static moto_ancs *client;
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void *context(session_t *s) { return (void *)s->epoch; }
static session_t *session_current(uint16_t conn, void *arg) {
    return session.conn == conn && session.epoch == (uintptr_t)arg && session.want_ancs ? &session : NULL;
}
static void disconnect_failed(session_t *s, const char *step, int status) {
    ESP_LOGW(TAG, "ANCS_ERROR step=%s status=%d", step, status);
    hud_display_ancs_clear();
    s->ready = false; s->busy = false; s->disconnect = true;
}
static int control_written(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg) {
    (void)attr;
    session_t *s = session_current(conn, arg);
    if (s) moto_ancs_write_result(client, s->ancs_epoch, error->status == 0, now_ms());
    return 0;
}
static int write_control(void *user, uint64_t epoch, const uint8_t *bytes, size_t size) {
    (void)user;
    if (!session.ready || session.ancs_epoch != epoch) return -1;
    // NimBLE copies the payload before returning.
    return ble_gattc_write_flat(session.conn, session.chars[CONTROL].value, bytes, size, control_written, context(&session));
}
static void notification(void *user, const moto_ancs_notification *notice) {
    (void)user;
    hud_display_ancs(notice);
    ESP_LOGI(TAG, "ANCS_NOTIFICATION uid=%" PRIu32 " category=%u title_bytes=%u text_bytes=%u (content not logged)",
        notice->uid, notice->category, (unsigned)strlen(notice->title), (unsigned)strlen(notice->text));
}
static void removed(void *user, uint32_t uid) { (void)user; hud_display_ancs_remove(uid); ESP_LOGI(TAG, "ANCS_REMOVED uid=%" PRIu32, uid); }
static void reset_required(void *user) { (void)user; hud_display_ancs_clear(); session.disconnect = true; }
static void action_submitted(void *user, uint32_t uid, bool success) {
    (void)user; ESP_LOGI(TAG, "ANCS_ACTION uid=%" PRIu32 " submitted=%d", uid, success);
}
static int subscription_written(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg);
static void subscribe_next(session_t *s)
{
    static const int order[] = {CHANGED, DATA, SOURCE};
    while (s->subscribe_index < 3 && !s->chars[order[s->subscribe_index]].cccd) {
        if (order[s->subscribe_index] != CHANGED) { disconnect_failed(s, "missing_cccd", -1); return; }
        s->subscribe_index++;
    }
    if (s->subscribe_index == 3) {
        s->busy = false; s->ready = true;
        s->ancs_epoch = moto_ancs_start(client, true);
        for (unsigned i = 0; i < s->early_count; ++i)
            moto_ancs_source(client, s->ancs_epoch, s->early[i], 8, now_ms());
        s->early_count = 0;
        ESP_LOGI(TAG, "ANCS_READY encrypted=%d dataSubscribed=1 sourceSubscribed=1", s->encrypted);
        return;
    }
    int index = order[s->subscribe_index];
    uint8_t value[] = {index == CHANGED ? 2 : 1, 0};
    int rc = ble_gattc_write_flat(s->conn, s->chars[index].cccd, value, sizeof value, subscription_written, context(s));
    s->deadline = now_ms() + 15000;
    if (rc) disconnect_failed(s, "subscribe_start", rc);
}

static int subscription_written(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    session_t *s = session_current(conn, arg);
    if (!s) return 0;
    if (error->status) { disconnect_failed(s, "subscribe", error->status); return 0; }
    s->subscribe_index++;
    subscribe_next(s);
    return 0;
}

static int descriptors_found(uint16_t conn, const struct ble_gatt_error *error, uint16_t chr_value,
                             const struct ble_gatt_dsc *dsc, void *arg);
static void descriptors_next(session_t *s)
{
    while (s->descriptor_index < CHAR_COUNT && (s->descriptor_index == CONTROL ||
           !s->chars[s->descriptor_index].value)) s->descriptor_index++;
    if (s->descriptor_index == CHAR_COUNT) { s->subscribe_index = 0; subscribe_next(s); return; }
    remote_char_t *chr = &s->chars[s->descriptor_index];
    if (chr->end <= chr->value) { disconnect_failed(s, "descriptor_range", -1); return; }
    int rc = ble_gattc_disc_all_dscs(s->conn, chr->value, chr->end, descriptors_found, context(s));
    s->deadline = now_ms() + 10000;
    if (rc) disconnect_failed(s, "descriptor_start", rc);
}

static int descriptors_found(uint16_t conn, const struct ble_gatt_error *error, uint16_t chr_value,
                             const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_value;
    session_t *s = session_current(conn, arg);
    if (!s) return 0;
    if (error->status == BLE_HS_EDONE) { s->descriptor_index++; descriptors_next(s); }
    else if (error->status) disconnect_failed(s, "descriptors", error->status);
    else if (ble_uuid_u16(&dsc->uuid.u) == 0x2902) s->chars[s->descriptor_index].cccd = dsc->handle;
    return 0;
}

static int characteristics_found(uint16_t conn, const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr, void *arg)
{
    session_t *s = session_current(conn, arg);
    if (!s) return 0;
    if (error->status == BLE_HS_EDONE) {
        if (!s->discovering_gatt && s->gatt_start) {
            s->discovering_gatt = true; s->last_char = -1;
            int rc = ble_gattc_disc_all_chrs(s->conn, s->gatt_start, s->gatt_end, characteristics_found, context(s));
            s->deadline = now_ms() + 10000;
            if (rc) disconnect_failed(s, "gatt_characteristics_start", rc);
        } else {
            if (!s->chars[DATA].value || !s->chars[SOURCE].value || !s->chars[CONTROL].value) {
                disconnect_failed(s, "missing_characteristic", -1); return 0;
            }
            s->descriptor_index = 0; descriptors_next(s);
        }
        return 0;
    }
    if (error->status) { disconnect_failed(s, "characteristics", error->status); return 0; }
    if (s->last_char >= 0) s->chars[s->last_char].end = chr->def_handle - 1;
    s->last_char = -1;
    for (int i = 0; i < CHAR_COUNT; i++) {
        bool match = i == CHANGED ? ble_uuid_u16(&chr->uuid.u) == 0x2a05 :
                                   ble_uuid_cmp(&chr->uuid.u, &ancs_chars[i].u) == 0;
        if (match) {
            s->chars[i].value = chr->val_handle;
            s->chars[i].end = s->discovering_gatt ? s->gatt_end : s->ancs_end;
            s->last_char = i;
        }
    }
    return 0;
}

static int services_found(uint16_t conn, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *svc, void *arg)
{
    session_t *s = session_current(conn, arg);
    if (!s) return 0;
    if (error->status == BLE_HS_EDONE) {
        if (!s->ancs_start) {
            s->busy = false; s->retry_at = now_ms() + 3000;
            ESP_LOGI(TAG, "ANCS_WAIT service unavailable; allow Share System Notifications on iPhone");
        } else {
            s->last_char = -1; s->discovering_gatt = false;
            int rc = ble_gattc_disc_all_chrs(s->conn, s->ancs_start, s->ancs_end, characteristics_found, context(s));
            s->deadline = now_ms() + 10000;
            if (rc) disconnect_failed(s, "characteristics_start", rc);
        }
        return 0;
    }
    if (error->status) { disconnect_failed(s, "services", error->status); return 0; }
    if (ble_uuid_cmp(&svc->uuid.u, &ancs_service.u) == 0) {
        s->ancs_start = svc->start_handle; s->ancs_end = svc->end_handle;
    } else if (ble_uuid_u16(&svc->uuid.u) == 0x1801) {
        s->gatt_start = svc->start_handle; s->gatt_end = svc->end_handle;
    }
    return 0;
}

static void discover(session_t *s)
{
    s->busy = true;
    s->ancs_start = s->ancs_end = s->gatt_start = s->gatt_end = 0;
    memset(s->chars, 0, sizeof s->chars);
    s->deadline = now_ms() + 10000;
    int rc = ble_gattc_disc_all_svcs(s->conn, services_found, context(s));
    if (rc) disconnect_failed(s, "services_start", rc);
}


void terminal_ancs_init(void) {
    moto_ancs_config config = {NULL, write_control, notification, removed, reset_required, action_submitted};
    client = moto_ancs_create(&config); assert(client);
}
void terminal_ancs_connect(uint16_t connection) {
    hud_display_ancs_clear();
    moto_ancs_stop(client); memset(&session, 0, sizeof session);
    session.conn = connection; session.epoch = ++next_epoch;
}
void terminal_ancs_disconnect(void) { terminal_ancs_connect(BLE_HS_CONN_HANDLE_NONE); }
void terminal_ancs_request(bool enabled) {
    if (!enabled) hud_display_ancs_clear();
    if (!enabled && session.want_ancs) { session.want_ancs = false; session.ready = false; session.disconnect = true; }
    else session.want_ancs = enabled;
}
bool terminal_ancs_ready(void *user) { (void)user; return session.want_ancs && session.encrypted && session.ready; }
bool terminal_ancs_should_disconnect(void) { return session.disconnect; }
void terminal_ancs_tick(bool authenticated) {
    session_t *s = &session;
    if (!authenticated || !s->want_ancs || s->disconnect || s->conn == BLE_HS_CONN_HANDLE_NONE) return;
    if (!s->encrypted) {
        if (!s->security_started) {
            s->security_started = true; s->deadline = now_ms() + 60000;
            int rc = ble_gap_security_initiate(s->conn);
            ESP_LOGI(TAG, "PAIRING_REQUEST allow Bluetooth pairing and notification sharing on iPhone");
            if (rc && rc != BLE_HS_EALREADY) disconnect_failed(s, "security_start", rc);
        } else if (now_ms() > s->deadline) disconnect_failed(s, "security_timeout", -1);
        return;
    }
    if (s->busy && now_ms() > s->deadline) { disconnect_failed(s, "discovery_timeout", -1); return; }
    if (!s->ready && !s->busy && now_ms() >= s->retry_at) discover(s);
    if (s->ready) moto_ancs_tick(client, now_ms());
}
int terminal_ancs_gap(struct ble_gap_event *event, bool authenticated) {
    session_t *s = &session;
    switch (event->type) {
    case BLE_GAP_EVENT_ENC_CHANGE: {
        if (event->enc_change.conn_handle != s->conn) break;
        struct ble_gap_conn_desc desc;
        if (event->enc_change.status || ble_gap_conn_find(s->conn, &desc)) {
            disconnect_failed(s, "encryption", event->enc_change.status); break;
        }
        s->encrypted = desc.sec_state.encrypted;
        ESP_LOGI(TAG, "BLE_SECURITY encrypted=%d bonded=%d", s->encrypted, desc.sec_state.bonded);
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Only the PSK-authenticated owner explicitly requesting ANCS may repair
        // its own stale OS bond. Never erase other peers or SDK credentials.
        struct ble_gap_conn_desc desc;
        if (authenticated && s->want_ancs && !s->bond_repaired &&
            event->repeat_pairing.conn_handle == s->conn && !ble_gap_conn_find(s->conn, &desc) &&
            !ble_store_util_delete_peer(&desc.peer_id_addr)) {
            s->bond_repaired = true;
            ESP_LOGI(TAG, "BOND_REPAIR authenticated current peer; request system pairing again");
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        ESP_LOGW(TAG, "BOND_MISMATCH existing bond preserved");
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.conn_handle != s->conn || !s->want_ancs || !s->encrypted || s->disconnect) break;
        uint16_t handle = event->notify_rx.attr_handle;
        if (s->chars[CHANGED].value && handle == s->chars[CHANGED].value) {
            disconnect_failed(s, "service_changed", 0); break;
        }
        uint8_t bytes[512]; uint16_t size;
        if (ble_hs_mbuf_to_flat(event->notify_rx.om, bytes, sizeof bytes, &size)) {
            disconnect_failed(s, "notification_size", -1); break;
        }
        moto_result result = MOTO_OK;
        if (s->chars[SOURCE].value && handle == s->chars[SOURCE].value) {
            if (s->ready) result = moto_ancs_source(client, s->ancs_epoch, bytes, size, now_ms());
            else if (size == 8 && s->early_count < 16) memcpy(s->early[s->early_count++], bytes, 8);
            else result = MOTO_FULL;
        } else if (s->ready && s->chars[DATA].value && handle == s->chars[DATA].value)
            result = moto_ancs_data(client, s->ancs_epoch, bytes, size, now_ms());
        if (result != MOTO_OK) disconnect_failed(s, "notification_parse", result);
        break;
    }
    default: break;
    }
    return 0;
}
