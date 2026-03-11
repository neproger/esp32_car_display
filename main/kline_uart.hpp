#pragma once

#include <stdint.h>

#include "esp_err.h"

struct KlineUartConfig {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int rx_buffer_size;
};

esp_err_t kline_uart_init(const KlineUartConfig &config);
void kline_uart_begin(unsigned long baud);
void kline_uart_end();
void kline_uart_send(uint8_t data);
bool kline_uart_receive(uint8_t *data, unsigned long timeout_ticks);
int kline_uart_tx_pin();
