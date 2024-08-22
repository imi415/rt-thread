#include <drivers/i2c.h>
#include <drivers/pin.h>
#include <fcntl.h>
#include <rtthread.h>
#include <unistd.h>

/* SDK Drivers */
#include "fsl_clock.h"
#include "fsl_inputmux.h"
#include "fsl_reset.h"
#include "fsl_smartdma.h"

/* App */
#include "app_camera.h"
#include "app_camera_init_data.h"

#define APP_CAMERA_FW_BASE    (0x04000000)
#define APP_CAMERA_RES_HOR    (160U)
#define APP_CAMERA_RES_VER    (120U)
#define APP_CAMERA_FRAME_BASE (0x04002000) /* 8kB */
#define APP_CAMERA_FRAME_SIZE (APP_CAMERA_RES_HOR * APP_CAMERA_RES_VER * 2)
#define APP_CAMERA_STACK_SIZE (64U)

#define APP_CAMERA_I2C_BUS  "i2c7"
#define APP_CAMERA_I2C_ADDR 0x21

#define APP_CAMERA_RST_PIN 51 /* P1_19 */
#define APP_CAMERA_PWR_PIN 50 /* P1_18 */

static const uint8_t s_BMP_HDR[14] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0};
static const uint8_t s_BMP_IH[40]  = {40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0};

static struct rt_i2c_bus_device *s_camera_i2c_bus;
static rt_sem_t                  s_camera_if_semaphore = RT_NULL;

static uint8_t *s_image_buffer = (uint8_t *)APP_CAMERA_FRAME_BASE;
static uint8_t *s_camera_stack = (uint8_t *)(APP_CAMERA_FRAME_BASE + APP_CAMERA_FRAME_SIZE);

static int  app_camera_encode_image(const int fd);
static void app_camera_if_callback(void *param);

int app_camera_init(void) {
    /* TODO: Initialize SmartDMA and camera I2C here. */

    s_camera_if_semaphore = rt_sem_create("CIF_SEM", 0UL, RT_IPC_FLAG_PRIO);
    if (s_camera_if_semaphore == RT_NULL) {
        rt_kprintf("Failed to create CIF semaphore.\n");
        return -1;
    }

    CLOCK_AttachClk(kMAIN_CLK_to_CLKOUT);
    CLOCK_SetClkDiv(kCLOCK_DivClkOut, 30U); /* 6MHz required for CIF */

    rt_pin_mode(APP_CAMERA_RST_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(APP_CAMERA_RST_PIN, 0);
    rt_thread_delay(rt_tick_from_millisecond(20));
    rt_pin_write(APP_CAMERA_RST_PIN, 1);
    rt_thread_delay(rt_tick_from_millisecond(100));

    s_camera_i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(APP_CAMERA_I2C_BUS);
    if (s_camera_i2c_bus == RT_NULL) {
        rt_kprintf("Failed to find I2C bus.\n");

        return -2;
    }

    struct rt_i2c_msg msg;

    uint8_t temp = 0x00U;

    msg.addr = APP_CAMERA_I2C_ADDR;
    msg.buf  = &temp;
    msg.len  = 1;

    msg.flags = RT_I2C_WR | RT_I2C_IGNORE_NACK;
    rt_i2c_transfer(s_camera_i2c_bus, &msg, 1);

    msg.flags = RT_I2C_RD | RT_I2C_IGNORE_NACK;
    rt_i2c_transfer(s_camera_i2c_bus, &msg, 1);

    for (size_t i = 0; i < ARRAY_SIZE(g_camera_init_data); i++) {
        uint8_t wr_buf[] = {g_camera_init_data[i].address, g_camera_init_data[i].data};

        msg.buf   = wr_buf;
        msg.len   = 2;
        msg.flags = RT_I2C_WR;

        if (rt_i2c_transfer(s_camera_i2c_bus, &msg, 1) != 1) {
            rt_kprintf("Failed to write camera register.\n");

            return -3;
        }
    }

    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0, kINPUTMUX_GpioPort0Pin4ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 1, kINPUTMUX_GpioPort0Pin11ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 2, kINPUTMUX_GpioPort0Pin5ToSmartDma);

    SMARTDMA_Init(APP_CAMERA_FW_BASE, s_smartdmaCameraFirmware, s_smartdmaCameraFirmwareSize);

    NVIC_SetPriority(SMARTDMA_IRQn, 4);
    EnableIRQ(SMARTDMA_IRQn);

    SMARTDMA_InstallCallback(app_camera_if_callback, s_camera_if_semaphore);

    rt_kprintf("Camera interface initialized.\n");

    return 0;
}

/**/
int app_camera_capture(const char *path) {
    int ret = 0;

    /* TODO: Capture one frame of image and store to FS. */

    smartdma_camera_param_t *camera_params = rt_malloc_align(sizeof(smartdma_camera_param_t), 4);
    if (!camera_params) {
        rt_kprintf("Failed to allocate CIF parameters.\n");
        return -1;
    }

    camera_params->p_buffer       = (uint32_t *)s_image_buffer;
    camera_params->smartdma_stack = (uint32_t *)s_camera_stack;

    SMARTDMA_Boot(kSMARTDMA_CameraWholeFrameQVGA, camera_params, 0x02);

    if (rt_sem_take(s_camera_if_semaphore, RT_WAITING_FOREVER) != RT_EOK) {
        rt_kprintf("Failed to acquire CIF semaphore.\n");
        ret = -3;

        goto free_params_exit;
    }

    const int img_fd = open(path, O_CREAT | O_RDWR);
    if (img_fd < 0) {
        int e_no = errno;
        rt_kprintf("Failed to open output path: %d\n", e_no);
        ret = -4;

        goto free_params_exit;
    }

    if (app_camera_encode_image(img_fd) < 0) {
        rt_kprintf("Failed to encode image.\n");
        ret = -5;

        goto close_fd_exit;
    }

close_fd_exit:
    close(img_fd);

free_params_exit:
    rt_free_align(camera_params);

    return ret;
}

static int app_camera_encode_image(const int fd) {
    int ret = 0;

    const int line_size = APP_CAMERA_RES_HOR * 3;

    uint8_t *buf = rt_malloc(line_size);
    if (buf == RT_NULL) {
        return -RT_ENOMEM;
    }

    const int file_size = 54 + line_size * APP_CAMERA_RES_VER;

    uint8_t *bmp_fh = &buf[0];
    uint8_t *bmp_ih = &buf[14];

    rt_memcpy(bmp_fh, s_BMP_HDR, 14);
    rt_memcpy(bmp_ih, s_BMP_IH, 40);

    bmp_fh[2] = (uint8_t)(file_size);
    bmp_fh[3] = (uint8_t)(file_size >> 8);
    bmp_fh[4] = (uint8_t)(file_size >> 16);
    bmp_fh[5] = (uint8_t)(file_size >> 24);

    bmp_ih[4] = (uint8_t)(APP_CAMERA_RES_HOR);
    bmp_ih[5] = (uint8_t)(APP_CAMERA_RES_HOR >> 8);
    bmp_ih[6] = (uint8_t)(APP_CAMERA_RES_HOR >> 16);
    bmp_ih[7] = (uint8_t)(APP_CAMERA_RES_HOR >> 24);

    bmp_ih[8]  = (uint8_t)(APP_CAMERA_RES_VER);
    bmp_ih[9]  = (uint8_t)(APP_CAMERA_RES_VER >> 8);
    bmp_ih[10] = (uint8_t)(APP_CAMERA_RES_VER >> 16);
    bmp_ih[11] = (uint8_t)(APP_CAMERA_RES_VER >> 24);

    if (write(fd, buf, 54) != 54) {
        ret = -RT_EIO;
        goto free_buf_exit;
    }

    for (size_t i = 0; i < APP_CAMERA_RES_VER; i++) {
        for (size_t j = 0; j < APP_CAMERA_RES_HOR; j++) {
            const uint16_t color_p = ((uint16_t *)s_image_buffer)[i * APP_CAMERA_RES_HOR + j];
            const uint8_t  r       = (color_p >> 11) & 0x1F;
            const uint8_t  g       = (color_p >> 5) & 0x3F;
            const uint8_t  b       = color_p & 0x1F;

            buf[j * 3]     = b << 3;
            buf[j * 3 + 1] = g << 2;
            buf[j * 3 + 2] = r << 3;
        }

        if (write(fd, buf, line_size) != line_size) {
            ret = -RT_EIO;
            goto free_buf_exit;
        }
    }

free_buf_exit:
    rt_free(buf);

    return ret;
}

static void app_camera_if_callback(void *param) {
    rt_sem_t sem = param;

    static volatile bool count = false;

    if (!count) {
        count = true;
    } else {
        count = false;
        SMARTDMA_Reset();
        rt_sem_release(sem);
    }
}

static int app_camera_command(int argc, char **argv) {
    rt_kprintf("Camera commands.\n");

    app_camera_capture("/webroot/test.bmp");

    return 0;
}

MSH_CMD_EXPORT_ALIAS(app_camera_command, camera, Camera operation);
INIT_APP_EXPORT(app_camera_init);