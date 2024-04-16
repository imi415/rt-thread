#ifndef LCD_GC9B71_H
#define LCD_GC9B71_H

#include "epd_common.h"

typedef enum {
    LCD_GC9B71_DIR_0   = 0x48U,
    LCD_GC9B71_DIR_90  = 0x28U,
    LCD_GC9B71_DIR_180 = 0x88U,
    LCD_GC9B71_DIR_270 = 0xE8U,
} lcd_gc9b71_direction_t;

typedef enum {
    LCD_GC9B71_GS256  = 0,
    LCD_GC9B71_RGB111 = 1,
    LCD_GC9B71_RGB444 = 3,
    LCD_GC9B71_RGB565 = 5,
    LCD_GC9B71_RGB666 = 6,
    LCD_GC9B71_RGB888 = 7,
} lcd_gc9b71_pixel_format_t;

typedef struct {
    uint8_t *init_struct;
    uint32_t init_struct_length;

    uint16_t ram_size_x;
    uint16_t ram_size_y;

    uint16_t ram_offset_x;
    uint16_t ram_offset_y;

    uint16_t size_x;
    uint16_t size_y;

    bool inversion;
    bool bgr_filter;
} gc9b71_panel_config_t;

typedef struct {
    void    *user_data;
    epd_cb_t cb;

    const gc9b71_panel_config_t *panel_config;
    lcd_gc9b71_direction_t       direction;
    lcd_gc9b71_pixel_format_t    pixel_format;
} lcd_gc9b71_t;

epd_ret_t lcd_gc9b71_init(lcd_gc9b71_t *lcd, const gc9b71_panel_config_t *config);
epd_ret_t lcd_gc9b71_enable_display(lcd_gc9b71_t *lcd, bool on);
epd_ret_t lcd_gc9b71_set_pixel_format(lcd_gc9b71_t *lcd, lcd_gc9b71_pixel_format_t format);
epd_ret_t lcd_gc9b71_set_direction(lcd_gc9b71_t *lcd, lcd_gc9b71_direction_t direction);
epd_ret_t lcd_gc9b71_set_inversion(lcd_gc9b71_t *lcd, bool invert);
epd_ret_t lcd_gc9b71_load(lcd_gc9b71_t *lcd, epd_coord_t *coord, const uint8_t *data);

#endif  // LCD_GC9B71_H
