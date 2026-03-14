#include "kline_uart.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"

namespace {

KlineUartConfig s_cfg = {
    .uart_num = UART_NUM_1,
    .tx_pin = -1,
    .rx_pin = -1,
    .rx_buffer_size = 256,
};
bool s_inited = false;
bool s_driver_active = false;

esp_err_t apply_uart_pins()
{
    return uart_set_pin(
        (uart_port_t)s_cfg.uart_num,
        s_cfg.tx_pin,
        s_cfg.rx_pin,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);
}

uart_config_t make_uart_config(unsigned long baud)
{
    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = static_cast<int>(baud);
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;
    return uart_cfg;
}

esp_err_t configure_uart(unsigned long baud)
{
    const uart_config_t uart_cfg = make_uart_config(baud);
    ESP_RETURN_ON_ERROR(
        uart_driver_install((uart_port_t)s_cfg.uart_num, s_cfg.rx_buffer_size, 0, 0, nullptr, 0),
        "kline_uart",
        "driver install failed");
    ESP_RETURN_ON_ERROR(
        uart_param_config((uart_port_t)s_cfg.uart_num, &uart_cfg),
        "kline_uart",
        "param config failed");
    ESP_RETURN_ON_ERROR(apply_uart_pins(), "kline_uart", "set pin failed");
    uart_flush_input((uart_port_t)s_cfg.uart_num);
    s_driver_active = true;
    return ESP_OK;
}

void release_uart()
{
    if (!s_driver_active) {
        return;
    }
    uart_wait_tx_done((uart_port_t)s_cfg.uart_num, pdMS_TO_TICKS(50));
    uart_flush_input((uart_port_t)s_cfg.uart_num);
    uart_driver_delete((uart_port_t)s_cfg.uart_num);
    s_driver_active = false;
}

} // namespace

esp_err_t kline_uart_init(const KlineUartConfig &config)
{
    s_cfg = config;

    if (s_cfg.rx_pin >= 0) {
        gpio_set_pull_mode((gpio_num_t)s_cfg.rx_pin, GPIO_PULLUP_ONLY);
    }
    if (s_cfg.tx_pin >= 0) {
        gpio_set_pull_mode((gpio_num_t)s_cfg.tx_pin, GPIO_PULLUP_ONLY);
    }

    ESP_RETURN_ON_ERROR(configure_uart(10400), "kline_uart", "configure uart failed");
    s_inited = true;
    return ESP_OK;
}

void kline_uart_begin(unsigned long baud)
{
    if (!s_inited) {
        return;
    }
    release_uart();
    configure_uart(baud);
}

void kline_uart_end()
{
    if (!s_inited) {
        return;
    }
    release_uart();
}

void kline_uart_send(uint8_t data)
{
    if (!s_inited) {
        return;
    }
    uart_write_bytes((uart_port_t)s_cfg.uart_num, &data, 1);
    uart_wait_tx_done((uart_port_t)s_cfg.uart_num, pdMS_TO_TICKS(20));
}

bool kline_uart_receive(uint8_t *data, unsigned long timeout_ticks)
{
    if (!s_inited || data == nullptr) {
        return false;
    }

    const int len = uart_read_bytes((uart_port_t)s_cfg.uart_num, data, 1, timeout_ticks);
    return len == 1;
}

int kline_uart_tx_pin()
{
    return s_cfg.tx_pin;
}
