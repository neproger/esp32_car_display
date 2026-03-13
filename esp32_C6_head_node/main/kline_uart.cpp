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

esp_err_t apply_uart_pins()
{
    return uart_set_pin(
        (uart_port_t)s_cfg.uart_num,
        s_cfg.tx_pin,
        s_cfg.rx_pin,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);
}

} // namespace

esp_err_t kline_uart_init(const KlineUartConfig &config)
{
    s_cfg = config;

    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = 10400;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_RETURN_ON_ERROR(uart_driver_install((uart_port_t)s_cfg.uart_num, s_cfg.rx_buffer_size, 0, 0, nullptr, 0), "kline_uart", "driver install failed");
    ESP_RETURN_ON_ERROR(uart_param_config((uart_port_t)s_cfg.uart_num, &uart_cfg), "kline_uart", "param config failed");
    ESP_RETURN_ON_ERROR(apply_uart_pins(), "kline_uart", "set pin failed");

    if (s_cfg.rx_pin >= 0) {
        gpio_set_pull_mode((gpio_num_t)s_cfg.rx_pin, GPIO_PULLUP_ONLY);
    }
    if (s_cfg.tx_pin >= 0) {
        gpio_set_pull_mode((gpio_num_t)s_cfg.tx_pin, GPIO_PULLUP_ONLY);
    }

    s_inited = true;
    return ESP_OK;
}

void kline_uart_begin(unsigned long baud)
{
    if (!s_inited) {
        return;
    }
    apply_uart_pins();
    uart_set_baudrate((uart_port_t)s_cfg.uart_num, baud);
    uart_flush_input((uart_port_t)s_cfg.uart_num);
}

void kline_uart_end()
{
    if (!s_inited) {
        return;
    }
    uart_flush_input((uart_port_t)s_cfg.uart_num);
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
