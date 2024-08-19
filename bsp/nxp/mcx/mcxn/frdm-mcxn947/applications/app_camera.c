#include <rtthread.h>

/* SDK Drivers */
#include "fsl_clock.h"
#include "fsl_inputmux.h"
#include "fsl_reset.h"
#include "fsl_smartdma.h"

/* App */
#include "app_camera_init_data.h"

#define APP_CAMERA_FW_BASE    (0x04000000)
#define APP_CAMERA_RES_HOR    (160U)
#define APP_CAMERA_RES_VER    (120U)
#define APP_CAMERA_FRAME_SIZE (APP_CAMERA_RES_HOR * APP_CAMERA_RES_VER * 2)
#define APP_CAMERA_STACK_SIZE (64U)

#define APP_CAMERA_I2C_BUS  "i2c7"
#define APP_CAMERA_I2C_ADDR 0x21

#define APP_CAMERA_RST_PIN 51 /* P1_19 */
#define APP_CAMERA_PWR_PIN 50 /* P1_18 */

static struct rt_i2c_bus_device *s_camera_i2c_bus;
static rt_sem_t                  s_camera_if_semaphore = RT_NULL;

static void app_camera_if_callback(void *param);

int app_camera_init(void) {
    /* TODO: Initialize SmartDMA and camera I2C here. */

    s_camera_if_semaphore = rt_sem_create("CIF_SEM", 0UL, RT_IPC_FLAG_PRIO);
    if (s_camera_if_semaphore == RT_NULL) {
        rt_kprintf("Failed to create CIF semaphore.\n");
        return -1;
    }

    s_camera_i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(APP_CAMERA_I2C_BUS);
    if (s_camera_i2c_bus == RT_NULL) {
        rt_kprintf("Failed to find I2C bus.\n");

        return -2;
    }

    RESET_PeripheralReset(kSMART_DMA_RST_SHIFT_RSTn);
    CLOCK_EnableClock(kCLOCK_Smartdma);

    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0, kINPUTMUX_GpioPort0Pin4ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 1, kINPUTMUX_GpioPort0Pin11ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 2, kINPUTMUX_GpioPort0Pin5ToSmartDma);

    SMARTDMA_Init(APP_CAMERA_FW_BASE, s_smartdmaCameraFirmware, s_smartdmaCameraFirmwareSize);

    NVIC_SetPriority(SMARTDMA_IRQn, 4);
    EnableIRQ(SMARTDMA_IRQn);

    SMARTDMA_InstallCallback(app_camera_if_callback, s_camera_if_semaphore);

    CLOCK_AttachClk(kMAIN_CLK_to_CLKOUT);
    CLOCK_SetClkDiv(kCLOCK_DivClkOut, 25U); /* 6MHz required for CIF */

    rt_kprintf("Camera interface initialized.\n");

    return 0;
}

int app_camera_capture(uint8_t *image_name) {
    int ret = 0;

    /* TODO: Capture one frame of image and store to FS. */

    uint8_t *frame = rt_malloc_align(APP_CAMERA_FRAME_SIZE, 4);
    if (!frame) {
        rt_kprintf("Failed to allocate image buffer.\n");
        return -1;
    }

    uint8_t *stack = rt_malloc_align(APP_CAMERA_STACK_SIZE, 4);
    if (!stack) {
        rt_kprintf("Failed to allocate CIF stack.\n");

        ret = -1;
        goto free_frame_exit;
    }

    smartdma_camera_param_t *camera_params = rt_malloc_align(sizeof(smartdma_camera_param_t), 4);
    if (!camera_params) {
        rt_kprintf("Failed to allocate CIF parameters.\n");

        ret = -2;
        goto free_stack_exit;
    }

    camera_params->p_buffer       = (uint32_t *)frame;
    camera_params->smartdma_stack = (uint32_t *)stack;

    SMARTDMA_Boot(kSMARTDMA_CameraWholeFrameQVGA, camera_params, 0x02);

    if (rt_sem_take(s_camera_if_semaphore, RT_WAITING_FOREVER) != RT_EOK) {
        rt_kprintf("Failed to acquire CIF semaphore.\n");

        ret = -3;
    }

free_params_exit:
    rt_free_align(camera_params);

free_stack_exit:
    rt_free_align(stack);

free_frame_exit:
    rt_free_align(frame);

    return ret;
}

static void app_camera_if_callback(void *param) {
    rt_sem_t sem = param;

    SMARTDMA_Reset();

    rt_sem_release(sem);
}

static int app_camera_command(int argc, char **argv) {
    rt_kprintf("Camera commands.\n");

    return 0;
}

MSH_CMD_EXPORT_ALIAS(app_camera_command, camera, Camera operation);
INIT_APP_EXPORT(app_camera_init);