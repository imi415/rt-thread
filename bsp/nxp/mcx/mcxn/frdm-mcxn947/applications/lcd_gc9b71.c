#include "lcd_gc9b71.h"

#include "epd_common.h"

#define EPD_ASSERT(x) if(!(x)) for(;;) { /* ABORT. */}
#define EPD_ERROR_CHECK(x) if(x != EPD_OK) return EPD_FAIL

/**
 * @brief Execute command sequence.
 * Sequence format: 1 byte parameter length, 1 byte command, [length] bytes params.
 * Parameter length does not include command itself.
 * @param cb epd_cb_t callback
 * @param user_data user pointer
 * @param seq sequence array
 * @param seq_len sequence length
 * @return epd_ret_t
 */
epd_ret_t epd_common_execute_sequence(epd_cb_t *cb, void *user_data, uint8_t *seq, uint32_t seq_len) {
    EPD_ASSERT(cb->write_command_cb != NULL);

    uint32_t i = 0;
    while(i < seq_len) {
        EPD_ERROR_CHECK(cb->write_command_cb(user_data, &seq[i + 1], seq[i] + 1));
        i += seq[i] + 2;
    }

    return EPD_OK;
}

/* TODO: Summarize MIPI DCS common commands into a separate file. */
#define GC9B71_CMD_SWRESET (0x01U)
#define GC9B71_CMD_SLPIN   (0x10U)
#define GC9B71_CMD_SLPOUT  (0x11U)
#define GC9B71_CMD_INVOFF  (0x20U)
#define GC9B71_CMD_INVON   (0x21U)
#define GC9B71_CMD_DISPOFF (0x28U)
#define GC9B71_CMD_DISPON  (0x29U)
#define GC9B71_CMD_CASET   (0x2AU)
#define GC9B71_CMD_RASET   (0x2BU)
#define GC9B71_CMD_RAMWR   (0x2CU)
#define GC9B71_CMD_MADCTL  (0x36U)
#define GC9B71_CMD_COLMOD  (0x3AU)

static epd_ret_t lcd_gc9b71_reset(lcd_gc9b71_t *lcd);
static epd_ret_t lcd_gc9b71_sleep(lcd_gc9b71_t *lcd, bool sleep_mode);
static epd_ret_t lcd_gc9b71_panel_config(lcd_gc9b71_t *lcd, const gc9b71_panel_config_t *config);
static epd_ret_t lcd_gc9b71_set_window(lcd_gc9b71_t *lcd, epd_coord_t *coord);

epd_ret_t lcd_gc9b71_init(lcd_gc9b71_t *lcd, const gc9b71_panel_config_t *config) {
    EPD_ERROR_CHECK(lcd_gc9b71_reset(lcd));
    EPD_ERROR_CHECK(lcd->cb.delay_cb(lcd->user_data, 5));
    EPD_ERROR_CHECK(lcd_gc9b71_panel_config(lcd, config));
    EPD_ERROR_CHECK(lcd_gc9b71_set_pixel_format(lcd, LCD_GC9B71_RGB565));
    EPD_ERROR_CHECK(lcd_gc9b71_set_direction(lcd, LCD_GC9B71_DIR_0));
    EPD_ERROR_CHECK(lcd_gc9b71_set_inversion(lcd, false));
    EPD_ERROR_CHECK(lcd_gc9b71_sleep(lcd, false));
    EPD_ERROR_CHECK(lcd->cb.delay_cb(lcd->user_data, 120));

    if (lcd->cb.backlight_cb) {
        EPD_ERROR_CHECK(lcd->cb.backlight_cb(lcd, 1));
    }

    return EPD_OK;
}

epd_ret_t lcd_gc9b71_load(lcd_gc9b71_t *lcd, epd_coord_t *coord, const uint8_t *data) {
    uint32_t pixel_count = (coord->y_end - coord->y_start + 1) * (coord->x_end - coord->x_start + 1);

    uint32_t data_len = 0;

    switch (lcd->pixel_format) {
        case LCD_GC9B71_RGB444:
            data_len = pixel_count * 3 / 2;
            break;
        case LCD_GC9B71_RGB565:
            data_len = pixel_count * 2;
            break;
        case LCD_GC9B71_RGB666:
        case LCD_GC9B71_RGB888:
            data_len = pixel_count * 3;
            break;
        default:
            data_len = pixel_count;
            break;
    }

    // Set cursor
    EPD_ERROR_CHECK(lcd_gc9b71_set_window(lcd, coord));

    // Write pixel data
    EPD_ERROR_CHECK(lcd->cb.write_data_cb(lcd->user_data, data, data_len));

    return EPD_OK;
}

epd_ret_t lcd_gc9b71_set_pixel_format(lcd_gc9b71_t *lcd, lcd_gc9b71_pixel_format_t format) {
    lcd->pixel_format = format;

    uint8_t command[2] = {GC9B71_CMD_COLMOD, format};
    EPD_ERROR_CHECK(lcd->cb.write_command_cb(lcd->user_data, command, 0x02));

    return EPD_OK;
}

epd_ret_t lcd_gc9b71_set_direction(lcd_gc9b71_t *lcd, lcd_gc9b71_direction_t direction) {
    lcd->direction = direction;

    uint8_t command[2] = {GC9B71_CMD_MADCTL, direction};

    if (!lcd->panel_config->bgr_filter) {
        command[1] &= ~0x08U;
    }

    EPD_ERROR_CHECK(lcd->cb.write_command_cb(lcd->user_data, command, 0x02));

    return EPD_OK;
}

epd_ret_t lcd_gc9b71_set_inversion(lcd_gc9b71_t *lcd, bool invert) {
    uint8_t command[1];

    if (lcd->panel_config->inversion) {
        command[0] = invert ? GC9B71_CMD_INVOFF : GC9B71_CMD_INVON;
    } else {
        command[0] = invert ? GC9B71_CMD_INVON : GC9B71_CMD_INVOFF;
    }

    EPD_ERROR_CHECK(lcd->cb.write_command_cb(lcd->user_data, command, 0x01));

    return EPD_OK;
}

epd_ret_t lcd_gc9b71_enable_display(lcd_gc9b71_t *lcd, bool on) {
    uint8_t command[1];

    if (on) {
        command[0] = GC9B71_CMD_DISPON;
    } else {
        command[0] = GC9B71_CMD_DISPOFF;
    }

    EPD_ERROR_CHECK(lcd->cb.write_command_cb(lcd->user_data, command, 0x01));

    return EPD_OK;
}

static epd_ret_t lcd_gc9b71_reset(lcd_gc9b71_t *lcd) {
    uint8_t cmd_buf[1] = {GC9B71_CMD_SWRESET};

    if (lcd->cb.reset_cb) {
        return lcd->cb.reset_cb(lcd->user_data);
    }

    return lcd->cb.write_command_cb(lcd->user_data, cmd_buf, 1U);
}

static epd_ret_t lcd_gc9b71_sleep(lcd_gc9b71_t *lcd, bool sleep_mode) {
    uint8_t cmd_buf[1] = {GC9B71_CMD_SLPOUT};

    if (sleep_mode) {
        cmd_buf[0] = GC9B71_CMD_SLPIN;
    }

    return lcd->cb.write_command_cb(lcd->user_data, cmd_buf, 1U);
}

static epd_ret_t lcd_gc9b71_panel_config(lcd_gc9b71_t *lcd, const gc9b71_panel_config_t *config) {
    EPD_ERROR_CHECK(
            epd_common_execute_sequence(&lcd->cb, lcd->user_data, config->init_struct, config->init_struct_length));

    lcd->panel_config = config;
    return EPD_OK;
}

static epd_ret_t lcd_gc9b71_set_window(lcd_gc9b71_t *lcd, epd_coord_t *coord) {
    uint16_t real_x_start, real_x_end, real_y_start, real_y_end;
    uint16_t x_offset, y_offset;

    switch (lcd->direction) {
        case LCD_GC9B71_DIR_0:
            x_offset = lcd->panel_config->ram_offset_x;
            y_offset = lcd->panel_config->ram_offset_y;
            break;
        case LCD_GC9B71_DIR_90:
            x_offset = lcd->panel_config->ram_offset_y;
            y_offset = lcd->panel_config->ram_offset_x;
            break;
        case LCD_GC9B71_DIR_180:
            x_offset = lcd->panel_config->ram_size_x - (lcd->panel_config->ram_offset_x + lcd->panel_config->size_x);
            y_offset = lcd->panel_config->ram_size_y - (lcd->panel_config->ram_offset_y + lcd->panel_config->size_y);
            break;
        case LCD_GC9B71_DIR_270:
            x_offset = lcd->panel_config->ram_size_y - (lcd->panel_config->ram_offset_y + lcd->panel_config->size_y);
            y_offset = lcd->panel_config->ram_size_x - (lcd->panel_config->ram_offset_x + lcd->panel_config->size_x);
            break;
        default:
            x_offset = 0;
            y_offset = 0;
    }

    real_x_start = coord->x_start + x_offset;
    real_x_end   = coord->x_end + x_offset;
    real_y_start = coord->y_start + y_offset;
    real_y_end   = coord->y_end + y_offset;

    uint8_t tx_buf[5] = {
            GC9B71_CMD_CASET,       ((uint8_t)(real_x_start >> 0x08U) & 0xFFU),
            (real_x_start & 0xFFU), ((uint8_t)(real_x_end >> 0x08U) & 0xFFU),
            (real_x_end & 0xFFU),
    };

    EPD_ERROR_CHECK(lcd->cb.write_command_cb(lcd->user_data, tx_buf, 0x05));

    tx_buf[0] = GC9B71_CMD_RASET;
    tx_buf[1] = ((uint8_t)(real_y_start >> 0x08U) & 0xFFU);
    tx_buf[2] = (real_y_start & 0xFFU);
    tx_buf[3] = ((uint8_t)(real_y_end >> 0x08U) & 0xFFU);
    tx_buf[4] = (real_y_end & 0xFFU);

    EPD_ERROR_CHECK(lcd->cb.write_command_cb(lcd->user_data, tx_buf, 0x05));
    return EPD_OK;
}