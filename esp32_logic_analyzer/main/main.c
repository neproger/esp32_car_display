#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "UART_SNIFF";

#define SNIFF_BAUDRATE 10400
#define UART_RX_BUF_SIZE 256

typedef struct {
    uart_port_t uart_num;
    int rx_pin;
    const char *label;
} sniff_port_t;

static void sniff_uart_init(const sniff_port_t *port)
{
    const uart_config_t cfg = {
        .baud_rate = SNIFF_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(port->uart_num, UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port->uart_num, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port->uart_num, UART_PIN_NO_CHANGE, port->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(port->uart_num, UART_MODE_UART));
    ESP_LOGI(TAG, "%s on GPIO%d at %d baud", port->label, port->rx_pin, SNIFF_BAUDRATE);
}

static void sniff_task(void *arg)
{
    const sniff_port_t *port = (const sniff_port_t *)arg;
    uint8_t buf[64];

    while (1) {
        int len = uart_read_bytes(port->uart_num, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (len <= 0) {
            continue;
        }

        int64_t us = esp_timer_get_time();
        char line[256];
        int off = snprintf(line, sizeof(line), "[%lld us] %s:", (long long)us, port->label);
        for (int i = 0; i < len && off < (int)sizeof(line) - 4; ++i) {
            off += snprintf(line + off, sizeof(line) - off, " %02X", buf[i]);
        }
        ESP_LOGI(TAG, "%s", line);
    }
}

void app_main(void)
{
    static const sniff_port_t port26 = {
        .uart_num = UART_NUM_1,
        .rx_pin = 26,
        .label = "GPIO26",
    };
    static const sniff_port_t port27 = {
        .uart_num = UART_NUM_2,
        .rx_pin = 27,
        .label = "GPIO27",
    };

    sniff_uart_init(&port26);
    sniff_uart_init(&port27);

    xTaskCreate(sniff_task, "sniff26", 4096, (void *)&port26, 5, NULL);
    xTaskCreate(sniff_task, "sniff27", 4096, (void *)&port27, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
