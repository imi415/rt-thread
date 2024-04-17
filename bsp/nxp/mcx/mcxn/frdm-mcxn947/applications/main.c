/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * Copyright (c) 2019-2020, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-10-24     Magicoe      first version
 * 2020-01-10     Kevin/Karl   Add PS demo
 * 2020-09-21     supperthomas fix the main.c
 *
 */

#include <rtdevice.h>
#include <rtthread.h>
#include "drv_pin.h"

#include <lvgl.h>
#include <lv_demos.h>
#include "lcd_impl.h"

static lcd_gc9b71_t s_lcd = {
        .cb =
                {
                        .write_command_cb = app_lcd_impl_write_command,
                        .write_data_cb    = app_lcd_impl_write_data,
                        .delay_cb         = app_lcd_impl_delay,

                },
        .user_data = NULL,
};

static lv_disp_draw_buf_t s_disp_buf;
static lv_color_t s_disp_buf_color[320 * 120];
static lv_disp_drv_t s_disp_drv;

int main(void)
{
    while (1)
    {
        rt_thread_mdelay(1000);
    }
}

void app_lvgl_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    epd_coord_t coord = {
            .x_start = area->x1,
            .x_end = area->x2,
            .y_start = area->y1,
            .y_end = area->y2,
    };

    lcd_gc9b71_load(&s_lcd, &coord, (const uint8_t *)color_p);

    lv_disp_flush_ready(disp_drv);
}

void app_lvgl_round(struct _lv_disp_drv_t * disp_drv, lv_area_t * area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of area down to the nearest even number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;

    // round the end of area up to the nearest odd number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

void lv_port_disp_init(void) {
    app_lcd_impl_init(NULL);

    lcd_gc9b71_init(&s_lcd, &lcd_h189s001_panel_config);
    lcd_gc9b71_set_pixel_format(&s_lcd, LCD_GC9B71_RGB565);
    lcd_gc9b71_enable_display(&s_lcd, true);

    lv_disp_draw_buf_init(&s_disp_buf, s_disp_buf_color, NULL, 320 * 120);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.draw_buf = &s_disp_buf;
    s_disp_drv.flush_cb = app_lvgl_flush;
    s_disp_drv.rounder_cb = app_lvgl_round;
    s_disp_drv.hor_res = 320;
    s_disp_drv.ver_res = 386;
    lv_disp_drv_register(&s_disp_drv);
}

void lv_port_indev_init(void) {

}

void lv_user_gui_init(void) {
    lv_demo_music();
}