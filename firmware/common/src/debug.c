/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/uart.h"

static char *buffer_;
static bool *is_enabled_;
static uint baudrate_;

void debug_init(uint baudrate, char *buffer, bool *is_enabled) {
    buffer_ = buffer;
    baudrate_ = baudrate;
    is_enabled_ = is_enabled;
    if (*is_enabled_) {
        uart_init(uart0, baudrate);
        uart_set_fifo_enabled(uart0, true);
        gpio_set_function(DEBUG_UART_TX_GPIO, GPIO_FUNC_UART);
    }
}

void debug_reinit(void) {
    if (*is_enabled_) {
        uart_init(uart0, baudrate_);
        uart_set_fifo_enabled(uart0, true);
        gpio_set_function(DEBUG_UART_TX_GPIO, GPIO_FUNC_UART);
    }
}

void debug(const char *format, ...) {
    if (*is_enabled_) {
        va_list args;
        va_start(args, format);
        vsprintf(buffer_, format, args);
        uart_puts(uart0, buffer_);
        va_end(args);
    }
}

void debug_block(const char *format, ...) {
    if (*is_enabled_) {
        va_list args;
        va_start(args, format);
        vsprintf(buffer_, format, args);
        uart_puts(uart0, buffer_);
        va_end(args);
        uart_tx_wait_blocking(uart0);
    }
}

bool debug_is_enabled(void) { return *is_enabled_; }
