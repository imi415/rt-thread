/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-08-19     Yilin Sun    Initial version.
 */

#include <rtdevice.h>

#include "fsl_flexspi.h"

#include "drv_qspi_flexspi.h"

#if defined BSP_USING_QSPI_FLEXSPI

typedef struct mcx_qspi_flexspi
{
    struct rt_spi_bus spi_bus;
    FLEXSPI_Type *spi_instance;
    uint32_t input_freq;
    bool initialized;
} mcx_qspi_flexspi_obj_t;

static const uint32_t mcx_qspi_lut[4] = {
    0x00000000UL,
    0x00000000UL,
    0x00000000UL,
    FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, 0x9F, kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x00),
};

static FLEXSPI_Type *mcx_qspi_flexspi_instances[] = FLEXSPI_BASE_PTRS;
static mcx_qspi_flexspi_obj_t mcx_qspi_flexspi_list[ARRAY_SIZE(mcx_qspi_flexspi_instances)];

static rt_err_t mcx_qspi_flexspi_configure(struct rt_spi_device *device, struct rt_spi_configuration *configuration)
{
     mcx_qspi_flexspi_obj_t *spi = ((struct rt_qspi_device *)device)->parent.bus->parent.user_data;

    if(spi->initialized)
    {
        return  RT_EOK;
    }

    flexspi_config_t flexspi_cfg;
    FLEXSPI_GetDefaultConfig(&flexspi_cfg);

    flexspi_cfg.rxSampleClock                  = kFLEXSPI_ReadSampleClkLoopbackInternally;
    flexspi_cfg.ahbConfig.enableAHBPrefetch    = true;
    flexspi_cfg.ahbConfig.enableAHBBufferable  = true;
    flexspi_cfg.ahbConfig.enableAHBCachable    = true;
    flexspi_cfg.enableSameConfigForAll         = false;
    flexspi_cfg.ahbConfig.enableReadAddressOpt = true;


    FLEXSPI_Init(spi->spi_instance, &flexspi_cfg);

    flexspi_device_config_t dev_cfg = {
        .flexspiRootClk       = CLOCK_GetFlexspiClkFreq(),
        .flashSize            = 64 * 1024,
        .CSIntervalUnit       = kFLEXSPI_CsIntervalUnit1SckCycle,
        .CSInterval           = 2,
        .CSHoldTime           = 3,
        .CSSetupTime          = 3,
        .dataValidTime        = 2,
        .columnspace          = 0,
        .enableWordAddress    = false,
        .AWRSeqIndex          = 0,
        .AWRSeqNumber         = 1,
        .ARDSeqIndex          = 0,
        .ARDSeqNumber         = 1,
        .AHBWriteWaitUnit     = kFLEXSPI_AhbWriteWaitUnit2AhbCycle,
        .AHBWriteWaitInterval = 0,
        .enableWriteMask      = false,
    };

    FLEXSPI_SetFlashConfig(spi->spi_instance, &dev_cfg, kFLEXSPI_PortA1);
    FLEXSPI_UpdateLUT(spi->spi_instance, 0, mcx_qspi_lut, ARRAY_SIZE(mcx_qspi_lut));
    FLEXSPI_SoftwareReset(spi->spi_instance);

    spi->initialized = true;

    return RT_EOK;
}

static rt_ssize_t mcx_qspi_flexspi_xfer(struct rt_spi_device *device, struct rt_spi_message *message)
{
    struct rt_qspi_message *msg = (struct rt_qspi_message *)message;
    mcx_qspi_flexspi_obj_t *spi = ((struct rt_qspi_device *)device)->parent.bus->parent.user_data;

    flexspi_transfer_t xfer = {0};

    uint32_t access_lut[4] = {0U};
    uint32_t access_lut_idx = 0;

    xfer.port = kFLEXSPI_PortA1;
    xfer.seqIndex = 1;
    xfer.SeqNumber = 1;

    /* Command has command phase */
    if(msg->instruction.qspi_lines != 0)
    {
        uint8_t pad = msg->instruction.qspi_lines >> 1;
        uint8_t opr = msg->instruction.content;
        if(access_lut_idx % 2 == 0)
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, pad, opr, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00);
        } else
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00, kFLEXSPI_Command_SDR, pad, opr);
        }

        access_lut_idx++;
    }

    /* Command has modifier phase */
    if(msg->address.size)
    {
        uint8_t pad = msg->address.qspi_lines >> 1;
        uint8_t opr = msg->address.size;

        if(access_lut_idx % 2 == 0)
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_RADDR_SDR, pad, opr, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00);
        } else
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00, kFLEXSPI_Command_RADDR_SDR, pad, opr);
        }

        access_lut_idx ++;

        xfer.deviceAddress = msg->address.content;
    }

    if(msg->alternate_bytes.size)
    {
        uint8_t pad = msg->alternate_bytes.qspi_lines;
        uint8_t opr = msg->alternate_bytes.content;

        if(access_lut_idx % 2 == 0)
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_MODE8_SDR, pad, opr, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00);
        } else
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00, kFLEXSPI_Command_MODE8_SDR, pad, opr);
        }

        access_lut_idx++;
    }

    if(msg->dummy_cycles)
    {
        uint8_t opr = msg->dummy_cycles;

        if(access_lut_idx % 2 == 0)
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_4PAD, opr, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00);
        } else
        {
            access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00, kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_4PAD, opr);
        }

        access_lut_idx++;
    }

    if(msg->parent.length == 0)
    {
        xfer.cmdType = kFLEXSPI_Command;
    } else
    {
        xfer.dataSize = msg->parent.length;
        uint8_t pad = msg->qspi_data_lines >> 1U;

        if(msg->parent.send_buf == NULL)
        {
            xfer.cmdType = kFLEXSPI_Read;
            xfer.data = msg->parent.recv_buf;

            if(access_lut_idx % 2 == 0)
            {
                access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, pad, 0x00, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00);
            } else
            {
                access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00, kFLEXSPI_Command_READ_SDR, pad, 0x00);
            }
        } else
        {
            xfer.cmdType = kFLEXSPI_Write;
            xfer.data = (uint32_t *)msg->parent.send_buf;

            if(access_lut_idx % 2 == 0)
            {
                access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, pad, 0x00, kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00);
            } else
            {
                access_lut[access_lut_idx / 2] |= FLEXSPI_LUT_SEQ(kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00, kFLEXSPI_Command_WRITE_SDR, pad, 0x00);
            }
        }
    }

    FLEXSPI_UpdateLUT(spi->spi_instance, 4, access_lut, 4);

    status_t ret = FLEXSPI_TransferBlocking(spi->spi_instance, &xfer);
    if(ret != kStatus_Success)
    {
        return 0;
    }

    if(message->length != 0) return message->length;
    return 1;
}

static const struct rt_spi_ops mcx_qspi_flexspi_ops =
{
    .configure = mcx_qspi_flexspi_configure,
    .xfer = mcx_qspi_flexspi_xfer,
};

int mcx_qspi_flexspi_init(void)
{
    rt_err_t ret;

    char name_buf[16];

    for (unsigned int i = 0; i < ARRAY_SIZE(mcx_qspi_flexspi_instances); i++)
    {
        mcx_qspi_flexspi_list[i].spi_instance = mcx_qspi_flexspi_instances[i];
        mcx_qspi_flexspi_list[i].input_freq = CLOCK_GetFlexspiClkFreq();
        mcx_qspi_flexspi_list[i].spi_bus.parent.user_data = &mcx_qspi_flexspi_list[i];
        mcx_qspi_flexspi_list[i].initialized = false;

        rt_snprintf(name_buf, sizeof(name_buf), "fspi%d", i);

        ret = rt_qspi_bus_register(&mcx_qspi_flexspi_list[i].spi_bus, name_buf, &mcx_qspi_flexspi_ops);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }


    return 0;
}

INIT_BOARD_EXPORT(mcx_qspi_flexspi_init);

#endif