#ifndef EPD_COMMON_H
#define EPD_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum { EPD_OK = 0, EPD_FAIL } epd_ret_t;

typedef struct {
    epd_ret_t (*write_command_cb)(void *handle, uint8_t *command, uint32_t len);
    epd_ret_t (*write_data_cb)(void *handle, const uint8_t *data, uint32_t len);
    epd_ret_t (*reset_cb)(void *handle);
    epd_ret_t (*poll_busy_cb)(void *handle);
    epd_ret_t (*delay_cb)(void *handle, uint32_t msec);
    epd_ret_t (*backlight_cb)(void *handle, uint8_t on);
} epd_cb_t;

typedef struct {
    uint32_t x_start;
    uint32_t x_end;
    uint32_t y_start;
    uint32_t y_end;
} epd_coord_t;

#endif