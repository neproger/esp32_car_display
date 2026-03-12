#include "ble_service.hpp"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

namespace {

static const char *TAG = "ble_service";
static const char *kBleDeviceName = "Golf3 K-Line";
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint8_t s_adv_instance = 0;
static bool s_adv_configured = false;

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_advertise_start();

static struct os_mbuf *ble_make_adv_data()
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t *>(kBleDeviceName);
    fields.name_len = strlen(kBleDeviceName);
    fields.name_is_complete = 1;

    struct os_mbuf *data = os_msys_get_pkthdr(BLE_HS_ADV_MAX_SZ, 0);
    if (data == nullptr) {
        return nullptr;
    }

    const int rc = ble_hs_adv_set_fields_mbuf(&fields, data);
    if (rc != 0) {
        os_mbuf_free_chain(data);
        return nullptr;
    }

    return data;
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
    int rc = 0;
    if (!s_adv_configured) {
        struct ble_gap_ext_adv_params params;
        memset(&params, 0, sizeof(params));

        params.connectable = 1;
        params.scannable = 0;
        params.legacy_pdu = 0;
        params.own_addr_type = s_own_addr_type;
        params.primary_phy = BLE_HCI_LE_PHY_CODED;
        params.secondary_phy = BLE_HCI_LE_PHY_CODED;
        params.sid = 0;
        params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
        params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

        rc = ble_gap_ext_adv_configure(s_adv_instance, &params, nullptr, ble_gap_event_cb, nullptr);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gap_ext_adv_configure failed: %d", rc);
            return;
        }

        struct os_mbuf *data = ble_make_adv_data();
        if (data == nullptr) {
            ESP_LOGE(TAG, "ble_make_adv_data failed");
            return;
        }

        rc = ble_gap_ext_adv_set_data(s_adv_instance, data);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gap_ext_adv_set_data failed: %d", rc);
            os_mbuf_free_chain(data);
            return;
        }

        s_adv_configured = true;
    }

    rc = ble_gap_ext_adv_start(s_adv_instance, 0, 0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_ext_adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started: name='%s', phy=coded", kBleDeviceName);
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

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    int rc = ble_svc_gap_device_name_set(kBleDeviceName);
    if (rc != 0) {
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}
