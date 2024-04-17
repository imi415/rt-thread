#include <rtthread.h>
#include <drv_qspi.h>
#include <drv_pin.h>

#include "lcd_gc9b71.h"

static struct rt_qspi_device *qspi_device = RT_NULL;

int app_lcd_impl_init(void *handle) {
    rt_hw_qspi_device_attach("qspi9", "qspi90", PIN_NONE, 4, RT_NULL, RT_NULL);

    qspi_device = (struct rt_qspi_device *) rt_device_find("qspi90");
    if(qspi_device == RT_NULL) {
        return -1;
    }

    struct rt_qspi_configuration cfg = {
            .parent = {
                    .max_hz = 48000000,
                    .mode = 0,
            },
            .qspi_dl_width = 4,
            .ddr_mode = 0,
    };

    rt_qspi_configure(qspi_device, &cfg);

    return 0;
}

epd_ret_t app_lcd_impl_write_command(void *handle, uint8_t *command, uint32_t len) {
    /* 02 - 00 - CMD - 00 - DATA0 - ... - DATAN */

    uint32_t address = (command[0] << 8U);

    struct rt_qspi_message msg = {
            .instruction = {
                    .content = 0x12,
                    .qspi_lines = 1,
            },
            .address = {
                    .content = address,
                    .size = 3,
                    .qspi_lines = 4,
            },
            .alternate_bytes = { 0 },
            .dummy_cycles = 0,
            .parent = {
                    .send_buf = &command[1],
                    .recv_buf = NULL,
                    .next = NULL,
                    .length = len - 1,
            },
            .qspi_data_lines = 4,
    };

    rt_qspi_transfer_message(qspi_device, &msg);
    return EPD_OK;
}

epd_ret_t app_lcd_impl_write_data(void *handle, const uint8_t *data, uint32_t len) {
    /* 32 - 00 - CMD(2C) - 00 - DATA0 - ... - DATAN */

    uint32_t address = (0x2C << 8U);

    struct rt_qspi_message msg = {
            .instruction = {
                    .content = 0x12,
                    .qspi_lines = 1,
            },
            .address = {
                    .content = address,
                    .size = 3,
                    .qspi_lines = 4,
            },
            .alternate_bytes = { 0 },
            .dummy_cycles = 0,
            .parent = {
                    .send_buf = data,
                    .recv_buf = NULL,
                    .next = NULL,
                    .length = len,
            },
            .qspi_data_lines = 4,
    };

    rt_qspi_transfer_message(qspi_device, &msg);
    return EPD_OK;
}

epd_ret_t app_lcd_impl_delay(void *handle, uint32_t msec) {
    rt_thread_mdelay(msec);
    return EPD_OK;
}