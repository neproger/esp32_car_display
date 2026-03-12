#include "ble_central_service.hpp"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"

namespace {

static const char *TAG = "ble_central";
static const char *kTargetName = "Golf3 K-Line";
static constexpr uint16_t kScanDurationForever = 0;
static constexpr gpio_num_t kButtonPin = GPIO_NUM_9;
static constexpr gpio_num_t kStatusLedPin = GPIO_NUM_8;
static constexpr int kBlinkOnMs = 60;
static constexpr int kBlinkOffMs = 70;
static const ble_uuid128_t kCommandServiceUuid128 = BLE_UUID128_INIT(
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x01, 0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);
static const ble_uuid128_t kCommandCharUuid128 = BLE_UUID128_INIT(
    0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x01, 0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);
static const ble_uuid128_t kResponseCharUuid128 = BLE_UUID128_INIT(
    0x12, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x01, 0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);
static const ble_uuid_t *kCommandServiceUuid = &kCommandServiceUuid128.u;
static const ble_uuid_t *kCommandCharUuid = &kCommandCharUuid128.u;
static const ble_uuid_t *kResponseCharUuid = &kResponseCharUuid128.u;

static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static bool s_connecting = false;
static bool s_connected = false;
static ble_addr_t s_peer_addr = {};
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_service_start_handle = 0;
static uint16_t s_service_end_handle = 0;
static uint16_t s_command_val_handle = 0;
static uint16_t s_response_val_handle = 0;
static led_strip_handle_t s_led_strip = nullptr;
static TaskHandle_t s_led_task_handle = nullptr;

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_start_scan();
static int service_discovery_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg);
static int command_characteristic_discovery_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg);
static int response_characteristic_discovery_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg);
static int command_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg);
static int response_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg);
static void button_task(void *arg);
static void led_task(void *arg);

static esp_err_t status_led_init()
{
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = kStatusLedPin;
    strip_config.max_leds = 1;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = 64;
    rmt_config.flags.with_dma = false;

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip), TAG, "status led init failed");
    ESP_RETURN_ON_ERROR(led_strip_clear(s_led_strip), TAG, "status led clear failed");
    return ESP_OK;
}

static void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_strip == nullptr) {
        return;
    }
    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    led_strip_refresh(s_led_strip);
}

static void signal_status(bool ok)
{
    if (s_led_task_handle == nullptr) {
        return;
    }
    xTaskNotify(s_led_task_handle, ok ? 1U : 2U, eSetValueWithOverwrite);
}

static void led_task(void *arg)
{
    (void)arg;
    uint32_t blink_count = 0;

    while (true) {
        if (xTaskNotifyWait(0, 0xffffffff, &blink_count, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        for (uint32_t i = 0; i < blink_count; ++i) {
            led_set_color(0, 32, 0);
            vTaskDelay(pdMS_TO_TICKS(kBlinkOnMs));
            led_strip_clear(s_led_strip);
            if (i + 1 < blink_count) {
                vTaskDelay(pdMS_TO_TICKS(kBlinkOffMs));
            }
        }
    }
}

static esp_err_t button_init()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << kButtonPin;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "button gpio config failed");
    return ESP_OK;
}

static void start_service_discovery(uint16_t conn_handle)
{
    s_service_start_handle = 0;
    s_service_end_handle = 0;
    s_command_val_handle = 0;
    s_response_val_handle = 0;

    const int rc = ble_gattc_disc_svc_by_uuid(conn_handle, kCommandServiceUuid, service_discovery_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_svc_by_uuid failed: %d", rc);
    }
}

static int service_discovery_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;

    if (error->status == 0 && service != nullptr) {
        s_service_start_handle = service->start_handle;
        s_service_end_handle = service->end_handle;
        return 0;
    }

    if (error->status == BLE_HS_EDONE && s_service_start_handle != 0) {
        const int rc = ble_gattc_disc_chrs_by_uuid(
            conn_handle,
            s_service_start_handle,
            s_service_end_handle,
            kCommandCharUuid,
            command_characteristic_discovery_cb,
            nullptr);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gattc_disc_chrs_by_uuid failed: %d", rc);
        }
        return 0;
    }

    ESP_LOGW(TAG, "Service discovery finished with status=%d", error->status);
    return 0;
}

static int command_characteristic_discovery_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (error->status == 0 && chr != nullptr) {
        s_command_val_handle = chr->val_handle;
        return 0;
    }

    if (error->status == BLE_HS_EDONE && s_command_val_handle != 0) {
        const int rc = ble_gattc_disc_chrs_by_uuid(
            conn_handle,
            s_service_start_handle,
            s_service_end_handle,
            kResponseCharUuid,
            response_characteristic_discovery_cb,
            nullptr);
        if (rc != 0) {
            ESP_LOGE(TAG, "response ble_gattc_disc_chrs_by_uuid failed: %d", rc);
        }
        return 0;
    }

    ESP_LOGW(TAG, "Command characteristic discovery finished with status=%d", error->status);
    return 0;
}

static int response_characteristic_discovery_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error->status == 0 && chr != nullptr) {
        s_response_val_handle = chr->val_handle;
        return 0;
    }

    if (error->status == BLE_HS_EDONE && s_response_val_handle != 0) {
        ESP_LOGI(
            TAG,
            "GATT ready: command_handle=%u response_handle=%u",
            s_command_val_handle,
            s_response_val_handle);
        return 0;
    }

    ESP_LOGW(TAG, "Response characteristic discovery finished with status=%d", error->status);
    return 0;
}

static int command_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    ESP_LOGI(TAG, "Command write complete: conn=%u status=%d", conn_handle, error->status);
    if (error->status == 0 && s_response_val_handle != 0) {
        const int rc = ble_gattc_read(conn_handle, s_response_val_handle, response_read_cb, nullptr);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gattc_read failed: %d", rc);
        }
    }
    return 0;
}

static int response_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;

    if (error->status != 0 || attr == nullptr || attr->om == nullptr) {
        ESP_LOGW(TAG, "Response read failed: conn=%u status=%d", conn_handle, error->status);
        return 0;
    }

    const uint16_t len = OS_MBUF_PKTLEN(attr->om);
    char buffer[64];
    const uint16_t copy_len = (len < (sizeof(buffer) - 1)) ? len : (sizeof(buffer) - 1);
    if (os_mbuf_copydata(attr->om, 0, copy_len, buffer) != 0) {
        ESP_LOGW(TAG, "Response read copy failed");
        return 0;
    }
    buffer[copy_len] = '\0';

    const char *status = nullptr;
    const char *suffix = strrchr(buffer, ':');
    if (suffix != nullptr && suffix[1] != '\0') {
        status = suffix + 1;
    }

    const bool ok = (status != nullptr) && (strcmp(status, "true") == 0);
    ESP_LOGI(TAG, "Response: %s", ok ? "true" : "false");
    signal_status(ok);
    return 0;
}

static void send_button_command()
{
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_command_val_handle == 0 || s_response_val_handle == 0) {
        ESP_LOGW(TAG, "Command path not ready");
        return;
    }

    static uint32_t counter = 0;
    char payload[32];
    snprintf(payload, sizeof(payload), "BTN:%lu", static_cast<unsigned long>(++counter));

    const int rc = ble_gattc_write_flat(
        s_conn_handle,
        s_command_val_handle,
        payload,
        strlen(payload),
        command_write_cb,
        nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_write_flat failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Command sent: %s", payload);
}

static void button_task(void *arg)
{
    (void)arg;
    int last_level = 1;

    while (true) {
        const int level = gpio_get_level(kButtonPin);
        if (last_level == 1 && level == 0) {
            send_button_command();
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static bool adv_name_matches(const uint8_t *data, uint8_t length)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    if (ble_hs_adv_parse_fields(&fields, data, length) != 0) {
        return false;
    }

    if (fields.name == nullptr || fields.name_len == 0) {
        return false;
    }

    return strlen(kTargetName) == fields.name_len &&
           memcmp(fields.name, kTargetName, fields.name_len) == 0;
}

static void ble_connect_to_peer(const ble_addr_t *addr)
{
    if (s_connecting || s_connected) {
        return;
    }

    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_disc_cancel failed: %d", rc);
    }

    memcpy(&s_peer_addr, addr, sizeof(s_peer_addr));
    s_connecting = true;

    rc = ble_gap_ext_connect(
        s_own_addr_type,
        &s_peer_addr,
        30000,
        BLE_GAP_LE_PHY_CODED_MASK,
        nullptr,
        nullptr,
        nullptr,
        ble_gap_event_cb,
        nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_ext_connect failed: %d", rc);
        s_connecting = false;
        ble_start_scan();
        return;
    }

    ESP_LOGI(
        TAG,
        "Connecting to %02x:%02x:%02x:%02x:%02x:%02x on coded PHY",
        s_peer_addr.val[5],
        s_peer_addr.val[4],
        s_peer_addr.val[3],
        s_peer_addr.val[2],
        s_peer_addr.val[1],
        s_peer_addr.val[0]);
}

static void ble_start_scan()
{
    struct ble_gap_ext_disc_params coded_params;
    memset(&coded_params, 0, sizeof(coded_params));
    coded_params.itvl = 0x50;
    coded_params.window = 0x30;
    coded_params.passive = 1;
    coded_params.disable_observer_mode = 0;

    int rc = ble_gap_ext_disc(
        s_own_addr_type,
        kScanDurationForever,
        0,
        1,
        0,
        0,
        nullptr,
        &coded_params,
        ble_gap_event_cb,
        nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_ext_disc failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Scanning for '%s' on coded PHY...", kTargetName);
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "nimble reset: reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_gap_set_prefered_default_le_phy(
        BLE_GAP_LE_PHY_CODED_MASK,
        BLE_GAP_LE_PHY_CODED_MASK);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_set_prefered_default_le_phy failed: %d", rc);
    }

    ble_start_scan();
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        if (!s_connecting && !s_connected &&
            adv_name_matches(event->ext_disc.data, event->ext_disc.length_data)) {
            ESP_LOGI(
                TAG,
                "Found target '%s' at %02x:%02x:%02x:%02x:%02x:%02x adv_phy=%u",
                kTargetName,
                event->ext_disc.addr.val[5],
                event->ext_disc.addr.val[4],
                event->ext_disc.addr.val[3],
                event->ext_disc.addr.val[2],
                event->ext_disc.addr.val[1],
                event->ext_disc.addr.val[0],
                event->ext_disc.prim_phy);
            ble_connect_to_peer(&event->ext_disc.addr);
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
    {
        s_connecting = false;
        if (event->connect.status != 0) {
            ESP_LOGE(TAG, "Connection failed: status=%d", event->connect.status);
            s_connected = false;
            ble_start_scan();
            return 0;
        }

        s_connected = true;
        s_conn_handle = event->connect.conn_handle;
        ESP_LOGI(TAG, "Connected, conn_handle=%u", event->connect.conn_handle);

        uint8_t tx_phy = 0;
        uint8_t rx_phy = 0;
        int rc = ble_gap_read_le_phy(event->connect.conn_handle, &tx_phy, &rx_phy);
        if (rc == 0) {
            ESP_LOGI(TAG, "Link PHY tx=%u rx=%u", tx_phy, rx_phy);
        } else {
            ESP_LOGW(TAG, "ble_gap_read_le_phy failed: %d", rc);
        }
        start_service_discovery(event->connect.conn_handle);
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
        s_connected = false;
        s_connecting = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_service_start_handle = 0;
        s_service_end_handle = 0;
        s_command_val_handle = 0;
        s_response_val_handle = 0;
        ble_start_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Scan complete, reason=%d", event->disc_complete.reason);
        if (!s_connecting && !s_connected) {
            ble_start_scan();
        }
        return 0;

    default:
        return 0;
    }

    return 0;
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

} // namespace

esp_err_t ble_central_service_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");

    err = nimble_port_init();
    ESP_RETURN_ON_ERROR(err, TAG, "nimble init failed");

    ESP_RETURN_ON_ERROR(button_init(), TAG, "button init failed");
    ESP_RETURN_ON_ERROR(status_led_init(), TAG, "status led init failed");

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);
    xTaskCreate(button_task, "button_task", 3072, nullptr, 4, nullptr);
    xTaskCreate(led_task, "led_task", 3072, nullptr, 3, &s_led_task_handle);
    return ESP_OK;
}
