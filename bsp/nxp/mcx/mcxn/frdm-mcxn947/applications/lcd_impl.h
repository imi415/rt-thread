#ifndef RTTHREAD_LCD_IMPL_H
#define RTTHREAD_LCD_IMPL_H

#include "lcd_h189s001.h"

int app_lcd_impl_init(void *handle);
epd_ret_t app_lcd_impl_write_command(void *handle, uint8_t *command, uint32_t len);
epd_ret_t app_lcd_impl_write_data(void *handle, const uint8_t *data, uint32_t len);
epd_ret_t app_lcd_impl_delay(void *handle, uint32_t msec);

#endif //RTTHREAD_LCD_IMPL_H
