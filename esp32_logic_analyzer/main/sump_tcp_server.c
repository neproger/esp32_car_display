#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "logic_analyzer_hal.h"

static const char *TAG = "LA_SUMP_TCP";

#define TCP_PORT 5555
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_RETRY 5

#define SUMP_RESET 0x00
#define SUMP_ARM 0x01
#define SUMP_QUERY 0x02
#define SUMP_SELF_TEST 0x03
#define SUMP_GET_METADATA 0x04
#define SUMP_SET_DIVIDER 0x80
#define SUMP_SET_READ_DELAY_COUNT 0x81
#define SUMP_SET_FLAGS 0x82
#define SUMP_SET_BIG_READ_CNT 0x83
#define SUMP_TRIGGER_MASK_CH_A 0xC0
#define SUMP_TRIGGER_VALUES_CH_A 0xC1
#define SUMP_TRIGGER_CONFIG_CH_A 0xC2
#define SUMP_TRIGGER_MASK_CH_B 0xC4
#define SUMP_TRIGGER_VALUES_CH_B 0xC5
#define SUMP_TRIGGER_CONFIG_CH_B 0xC6
#define SUMP_TRIGGER_MASK_CH_C 0xC8
#define SUMP_TRIGGER_VALUES_CH_C 0xC9
#define SUMP_TRIGGER_CONFIG_CH_C 0xCA
#define SUMP_TRIGGER_MASK_CH_D 0xCC
#define SUMP_TRIGGER_VALUES_CH_D 0xCD
#define SUMP_TRIGGER_CONFIG_CH_D 0xCE

#define PULSEVIEW_MAX_SAMPLE_RATE 100000000

#define LA_PIN_0 (CONFIG_ANALYZER_CHAN_0)
#define LA_PIN_1 (CONFIG_ANALYZER_CHAN_1)
#define LA_PIN_2 (CONFIG_ANALYZER_CHAN_2)
#define LA_PIN_3 (CONFIG_ANALYZER_CHAN_3)
#define LA_PIN_4 (CONFIG_ANALYZER_CHAN_4)
#define LA_PIN_5 (CONFIG_ANALYZER_CHAN_5)
#define LA_PIN_6 (CONFIG_ANALYZER_CHAN_6)
#define LA_PIN_7 (CONFIG_ANALYZER_CHAN_7)
#define LA_PIN_8 (CONFIG_ANALYZER_CHAN_8)
#define LA_PIN_9 (CONFIG_ANALYZER_CHAN_9)
#define LA_PIN_10 (CONFIG_ANALYZER_CHAN_10)
#define LA_PIN_11 (CONFIG_ANALYZER_CHAN_11)
#define LA_PIN_12 (CONFIG_ANALYZER_CHAN_12)
#define LA_PIN_13 (CONFIG_ANALYZER_CHAN_13)
#define LA_PIN_14 (CONFIG_ANALYZER_CHAN_14)
#define LA_PIN_15 (CONFIG_ANALYZER_CHAN_15)
#define LA_PIN_TRIGGER (CONFIG_ANALYZER_TRIG_PIN)
#define LA_PIN_EDGE (CONFIG_ANALYZER_TRIG_EDGE)
#define LA_SAMPLE_COUNT (CONFIG_ANALYZER_SAMPLES_COUNT)
#define LA_SAMPLE_RATE (CONFIG_ANALYZER_SAMPLE_RATE)
#define LA_ANALYZER_CHANNELS (CONFIG_ANALYZER_CHANNELS)
#define LA_ANALYZER_PSRAM (CONFIG_ANALYZER_PSRAM)
#if CONFIG_ANALYZER_TIMEOUT < 0
#define LA_DEFAULT_TIMEOUT (CONFIG_ANALYZER_TIMEOUT)
#else
#define LA_DEFAULT_TIMEOUT (CONFIG_ANALYZER_TIMEOUT * 100)
#endif

static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_num;
static int s_client_fd = -1;

static int s_first_trigger_pin = 0;
static int s_first_trigger_val = 0;
static int s_divider = 0;
static int s_read_count = 0;
static int s_delay_count = 0;

static logic_analyzer_config_t s_la_cfg = {
    .pin = {LA_PIN_0, LA_PIN_1, LA_PIN_2, LA_PIN_3, LA_PIN_4, LA_PIN_5, LA_PIN_6, LA_PIN_7,
            LA_PIN_8, LA_PIN_9, LA_PIN_10, LA_PIN_11, LA_PIN_12, LA_PIN_13, LA_PIN_14, LA_PIN_15},
    .pin_trigger = LA_PIN_TRIGGER,
    .trigger_edge = LA_PIN_EDGE,
    .number_of_samples = LA_SAMPLE_COUNT,
    .sample_rate = LA_SAMPLE_RATE,
    .number_channels = LA_ANALYZER_CHANNELS,
    .samples_to_psram = LA_ANALYZER_PSRAM,
    .meashure_timeout = LA_DEFAULT_TIMEOUT,
    .logic_analyzer_cb = NULL,
};

static logic_analyzer_hw_param_t s_la_hw;

static void tcp_write_data(const uint8_t *buf, int len)
{
    int sent = 0;
    while (sent < len && s_client_fd >= 0) {
        int ret = send(s_client_fd, buf + sent, len - sent, 0);
        if (ret <= 0) {
            ESP_LOGW(TAG, "send failed");
            close(s_client_fd);
            s_client_fd = -1;
            return;
        }
        sent += ret;
    }
}

static void tcp_write_byte(uint8_t byte)
{
    tcp_write_data(&byte, 1);
}

static bool tcp_read_exact(uint8_t *buf, int len)
{
    int read_total = 0;
    while (read_total < len && s_client_fd >= 0) {
        int ret = recv(s_client_fd, buf + read_total, len - read_total, 0);
        if (ret <= 0) {
            return false;
        }
        read_total += ret;
    }
    return read_total == len;
}

static void sump_get_metadata(void)
{
    ESP_LOGI(TAG, "send metadata");
    s_la_hw.current_channels = s_la_cfg.number_channels;
    s_la_hw.current_psram = s_la_cfg.samples_to_psram;
    logic_analyzer_get_hw_param(&s_la_hw);

    tcp_write_byte(0x01);
    tcp_write_data((const uint8_t *)"ESP32", 6);
    tcp_write_byte(0x02);
    tcp_write_data((const uint8_t *)"0.00", 5);

    uint32_t capture_size = (s_la_hw.current_channels > 4)
        ? (s_la_hw.max_sample_cnt * (s_la_hw.current_channels / 8))
        : s_la_hw.max_sample_cnt;
    tcp_write_byte(0x21);
    tcp_write_byte((capture_size >> 24) & 0xFF);
    tcp_write_byte((capture_size >> 16) & 0xFF);
    tcp_write_byte((capture_size >> 8) & 0xFF);
    tcp_write_byte(capture_size & 0xFF);

    uint32_t capture_speed = s_la_hw.max_sample_rate;
    tcp_write_byte(0x23);
    tcp_write_byte((capture_speed >> 24) & 0xFF);
    tcp_write_byte((capture_speed >> 16) & 0xFF);
    tcp_write_byte((capture_speed >> 8) & 0xFF);
    tcp_write_byte(capture_speed & 0xFF);

    tcp_write_byte(0x40);
    tcp_write_byte((s_la_hw.current_channels > 4) ? (uint8_t)s_la_hw.current_channels : 8);
    tcp_write_byte(0x41);
    tcp_write_byte(0x02);
    tcp_write_byte(0x00);
}

static void sump_la_cb(uint8_t *buf, int cnt, int clk, int channels)
{
    (void)clk;

    if (buf == NULL || s_client_fd < 0) {
        return;
    }

    int zero_sample = 0;
    int diff = s_read_count - cnt;

    if (channels == 8) {
        uint8_t *src = (uint8_t *)buf + s_read_count - 1 - diff;
        for (int i = 0; i < s_read_count; ++i) {
            if (i < diff) {
                tcp_write_data((const uint8_t *)&zero_sample, 1);
            } else {
                tcp_write_data(src, 1);
                src--;
            }
        }
    } else if (channels == 16) {
        uint16_t *src = (uint16_t *)buf + s_read_count - 1 - diff;
        for (int i = 0; i < s_read_count; ++i) {
            if (i < diff) {
                tcp_write_data((const uint8_t *)&zero_sample, 2);
            } else {
                tcp_write_data((const uint8_t *)src, 2);
                src--;
            }
        }
    } else {
        uint8_t *src = (uint8_t *)buf + (s_read_count / 2) - 1 - diff;
        for (int i = 0; i < s_read_count; ++i) {
            if (i < diff) {
                tcp_write_data((const uint8_t *)&zero_sample, 1);
            } else if (i & 1) {
                tcp_write_byte(*src & 0xF);
                src--;
            } else {
                tcp_write_byte((*src >> 4) & 0xF);
            }
        }
    }
}

static void sump_capture_and_send_samples(void)
{
    s_la_cfg.number_of_samples = s_read_count;
    s_la_cfg.sample_rate = PULSEVIEW_MAX_SAMPLE_RATE / (s_divider + 1);
    s_la_cfg.pin_trigger = (s_first_trigger_pin >= 0) ? s_la_cfg.pin[s_first_trigger_pin] : -1;
    s_la_cfg.trigger_edge = s_first_trigger_val ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE;
    s_la_cfg.logic_analyzer_cb = sump_la_cb;

    int err = start_logic_analyzer(&s_la_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start_logic_analyzer failed: %d", err);
    }
}

static bool sump_get_cmd4(uint8_t *cmd)
{
    return tcp_read_exact(cmd, 4);
}

static void sump_cmd_parser(uint8_t cmd_byte)
{
    static int trigger = 0;
    static int trigger_values = 0;
    union {
        uint32_t u32;
        uint16_t u16[2];
        uint8_t u8[4];
    } cmd = {0};

    ESP_LOGI(TAG, "cmd 0x%02X", cmd_byte);

    switch (cmd_byte) {
    case SUMP_RESET:
        // PulseView TCP scan may probe with RESET only; reply with the
        // standard SUMP signature so the device can be recognized.
        ESP_LOGI(TAG, "reply reset probe");
        tcp_write_data((const uint8_t *)"1ALS", 4);
        break;
    case SUMP_QUERY:
        ESP_LOGI(TAG, "reply query");
        tcp_write_data((const uint8_t *)"1ALS", 4);
        break;
    case SUMP_ARM:
        sump_capture_and_send_samples();
        break;
    case SUMP_TRIGGER_MASK_CH_A:
        if (!sump_get_cmd4(cmd.u8)) {
            return;
        }
        trigger = cmd.u32 & 0xFFFF;
        s_first_trigger_pin = -1;
        if (trigger) {
            for (int i = 0; i < 16; ++i) {
                if ((trigger >> i) & 0x1) {
                    s_first_trigger_pin = i;
                    break;
                }
            }
        }
        break;
    case SUMP_TRIGGER_VALUES_CH_A:
        if (!sump_get_cmd4(cmd.u8)) {
            return;
        }
        trigger_values = cmd.u32 & 0xFFFF;
        s_first_trigger_val = trigger ? ((trigger_values >> s_first_trigger_pin) & 1) : 0;
        break;
    case SUMP_TRIGGER_MASK_CH_B:
    case SUMP_TRIGGER_MASK_CH_C:
    case SUMP_TRIGGER_MASK_CH_D:
    case SUMP_TRIGGER_VALUES_CH_B:
    case SUMP_TRIGGER_VALUES_CH_C:
    case SUMP_TRIGGER_VALUES_CH_D:
    case SUMP_TRIGGER_CONFIG_CH_A:
    case SUMP_TRIGGER_CONFIG_CH_B:
    case SUMP_TRIGGER_CONFIG_CH_C:
    case SUMP_TRIGGER_CONFIG_CH_D:
    case SUMP_SET_FLAGS:
        if (!sump_get_cmd4(cmd.u8)) {
            return;
        }
        break;
    case SUMP_SET_DIVIDER:
        if (!sump_get_cmd4(cmd.u8)) {
            return;
        }
        s_divider = cmd.u32 & 0xFFFFFF;
        break;
    case SUMP_SET_READ_DELAY_COUNT:
        if (!sump_get_cmd4(cmd.u8)) {
            return;
        }
        s_read_count = (cmd.u16[0] + 1) * 4;
        s_delay_count = (cmd.u16[1] + 1) * 4;
        (void)s_delay_count;
        break;
    case SUMP_SET_BIG_READ_CNT:
        if (!sump_get_cmd4(cmd.u8)) {
            return;
        }
        s_read_count = (cmd.u32 + 1) * 4;
        break;
    case SUMP_GET_METADATA:
        sump_get_metadata();
        break;
    case SUMP_SELF_TEST:
    default:
        break;
    }
}

static void event_handler_sta(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_sta, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_sta, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    strcpy((char *)wifi_config.sta.ssid, CONFIG_ANALYZER_WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, CONFIG_ANALYZER_WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);

    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

static void tcp_server_task(void *arg)
{
    (void)arg;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(TCP_PORT),
    };

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "socket failed");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind failed");
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, 1) != 0) {
        ESP_LOGE(TAG, "listen failed");
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "SUMP TCP server listening on port %d", TCP_PORT);

    while (1) {
        struct sockaddr_in6 source_addr;
        socklen_t addr_len = sizeof(source_addr);
        s_client_fd = accept(listen_fd, (struct sockaddr *)&source_addr, &addr_len);
        if (s_client_fd < 0) {
            continue;
        }

        ESP_LOGI(TAG, "client connected");
        s_divider = 0;
        s_read_count = LA_SAMPLE_COUNT;
        s_delay_count = 0;
        s_first_trigger_pin = -1;
        s_first_trigger_val = 0;

        while (s_client_fd >= 0) {
            uint8_t cmd = 0;
            if (!tcp_read_exact(&cmd, 1)) {
                break;
            }
            sump_cmd_parser(cmd);
        }

        if (s_client_fd >= 0) {
            close(s_client_fd);
            s_client_fd = -1;
        }
        ESP_LOGI(TAG, "client disconnected");
    }
}

void logic_analyzer_sump_tcp_server(void)
{
    ESP_ERROR_CHECK(wifi_connect());
    xTaskCreate(tcp_server_task, "la_sump_tcp", 8192, NULL, 5, NULL);
}
