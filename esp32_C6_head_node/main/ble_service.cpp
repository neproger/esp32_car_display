#include "ble_service.hpp"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {

static const char *TAG = "ble_service";
static const char *kBleDeviceName = "Golf3 K-Line";
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
static constexpr gpio_num_t kStatusLedPin = GPIO_NUM_8;
static constexpr int64_t kStatusLedPulseUs = 80 * 1000;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint16_t s_command_val_handle = 0;
static uint16_t s_response_val_handle = 0;
static esp_timer_handle_t s_led_off_timer = nullptr;
static led_strip_handle_t s_led_strip = nullptr;
static char s_response_value[64] = "idle:false";

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_advertise_start();

static void led_off_timer_cb(void *arg)
{
    (void)arg;
    if (s_led_strip != nullptr) {
        led_strip_clear(s_led_strip);
    }
}

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

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip), TAG, "led strip init failed");
    ESP_RETURN_ON_ERROR(led_strip_clear(s_led_strip), TAG, "led strip clear failed");

    const esp_timer_create_args_t timer_args = {
        .callback = led_off_timer_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ble_led_off",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_led_off_timer), TAG, "status led timer create failed");
    return ESP_OK;
}

static void pulse_status_led()
{
    if (s_led_strip == nullptr) {
        return;
    }

    led_strip_set_pixel(s_led_strip, 0, 0, 0, 64);
    led_strip_refresh(s_led_strip);
    if (s_led_off_timer != nullptr) {
        esp_timer_stop(s_led_off_timer);
        esp_timer_start_once(s_led_off_timer, kStatusLedPulseUs);
    }
}

static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    char buffer[64];
    const uint16_t copy_len = (len < (sizeof(buffer) - 1)) ? len : (sizeof(buffer) - 1);
    const int rc = os_mbuf_copydata(ctxt->om, 0, copy_len, buffer);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    buffer[copy_len] = '\0';
    ESP_LOGI(TAG, "Command received: conn=%u payload='%s'", conn_handle, buffer);
    constexpr size_t kResponseSuffixLen = sizeof(":true") - 1;
    const size_t max_prefix_len = sizeof(s_response_value) - 1 - kResponseSuffixLen;
    const size_t response_prefix_len = (copy_len < max_prefix_len) ? copy_len : max_prefix_len;
    memcpy(s_response_value, buffer, response_prefix_len);
    memcpy(s_response_value + response_prefix_len, ":true", kResponseSuffixLen + 1);
    pulse_status_led();
    return 0;
}

static int response_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return os_mbuf_append(ctxt->om, s_response_value, strlen(s_response_value)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_chr_def kGattCharacteristics[] = {
    {
        .uuid = kCommandCharUuid,
        .access_cb = command_access_cb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &s_command_val_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = kResponseCharUuid,
        .access_cb = response_access_cb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = &s_response_val_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = nullptr,
        .access_cb = nullptr,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = 0,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
};

static const struct ble_gatt_svc_def kGattServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = kCommandServiceUuid,
        .includes = nullptr,
        .characteristics = kGattCharacteristics,
    },
    {
        .type = 0,
        .uuid = nullptr,
        .includes = nullptr,
        .characteristics = nullptr,
    },
};

static int ble_set_adv_fields()
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t *>(kBleDeviceName);
    fields.name_len = strlen(kBleDeviceName);
    fields.name_is_complete = 1;
    return ble_gap_adv_set_fields(&fields);
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "nimble reset: reason=%d", reason);
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
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
        ESP_LOGW(TAG, "set default coded phy failed: %d", rc);
    }

    ble_advertise_start();
}

static void ble_advertise_start()
{
    int rc = ble_set_adv_fields();
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER, &params, ble_gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started: name='%s'", kBleDeviceName);
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d", event->connect.status);
        if (event->connect.status != 0) {
            ble_advertise_start();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
        ble_advertise_start();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "adv complete reason=%d", event->adv_complete.reason);
        ble_advertise_start();
        return 0;

    default:
        return 0;
    }
}

} // namespace

esp_err_t ble_service_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");

    err = nimble_port_init();
    ESP_RETURN_ON_ERROR(err, TAG, "nimble init failed");
    ESP_RETURN_ON_ERROR(status_led_init(), TAG, "status led init failed");

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = 0;
    rc = ble_gatts_count_cfg(kGattServices);
    if (rc != 0) {
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(kGattServices);
    if (rc != 0) {
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    rc = ble_svc_gap_device_name_set(kBleDeviceName);
    if (rc != 0) {
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}
