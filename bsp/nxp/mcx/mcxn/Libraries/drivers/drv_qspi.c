/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-04-15     Yilin Sun    Initial revision
 */

#include <rtdevice.h>

#include "fsl_lpspi.h"

#include "drv_qspi.h"

#if defined(BSP_USING_QSPI)

typedef struct mcx_qspi
{
    struct rt_spi_bus spi_bus;
    LPSPI_Type *spi_instance;
    uint32_t input_freq;
    bool initialized;
} mcx_qspi_obj_t;

static LPSPI_Type *mcx_qspi_instances[] = LPSPI_BASE_PTRS;
static mcx_qspi_obj_t mcx_qspi_list[ARRAY_SIZE(mcx_qspi_instances)];

static rt_err_t mcx_qspi_configure(struct rt_spi_device *device, struct rt_spi_configuration *configuration)
{
    mcx_qspi_obj_t *spi = ((struct rt_qspi_device *) device)->parent.bus->parent.user_data;

    if (spi->initialized)
    {
        return RT_EOK;
    }

    lpspi_master_config_t lpspi_cfg;
    LPSPI_MasterGetDefaultConfig(&lpspi_cfg);

    lpspi_cfg.baudRate = configuration->max_hz;
    lpspi_cfg.pcsFunc = kLPSPI_PcsAsData;
    lpspi_cfg.pcsActiveHighOrLow = kLPSPI_PcsActiveLow;
    lpspi_cfg.whichPcs = kLPSPI_Pcs0;
    lpspi_cfg.dataOutConfig = kLpspiDataOutTristate;

    if (configuration->mode == 3)
    {
        lpspi_cfg.cpol = kLPSPI_ClockPolarityActiveHigh;
        lpspi_cfg.cpha = kLPSPI_ClockPhaseSecondEdge;
    }

    lpspi_cfg.betweenTransferDelayInNanoSec = (1000000000UL / lpspi_cfg.baudRate) / 2;
    lpspi_cfg.pcsToSckDelayInNanoSec = (1000000000UL / lpspi_cfg.baudRate) / 2;
    lpspi_cfg.lastSckToPcsDelayInNanoSec = (1000000000UL / lpspi_cfg.baudRate) / 2;

    LPSPI_MasterInit(spi->spi_instance, &lpspi_cfg, spi->input_freq);
    LPSPI_SetFifoWatermarks(spi->spi_instance, 7, 7);

    spi->initialized = true;

    return RT_EOK;
}

static rt_ssize_t mcx_qspi_xfer(struct rt_spi_device *device, struct rt_spi_message *message)
{
    struct rt_qspi_message *msg = (struct rt_qspi_message *) message;
    mcx_qspi_obj_t *qspi = device->bus->parent.user_data;
    LPSPI_Type *spi = qspi->spi_instance;
    uint8_t tx_fifo_size = LPSPI_GetTxFifoSize(spi);
    uint8_t rx_fifo_size = LPSPI_GetRxFifoSize(spi);

    uint8_t ca_buf[32];

    /* Basic transaction parameters, 8bit, TX only, CS0 */
    uint32_t tcr_base = LPSPI_TCR_FRAMESZ(7) | LPSPI_TCR_PCS(0) | LPSPI_TCR_CONT(1) | LPSPI_TCR_CONTC(1);

    /* Prepare module for new transaction */
    LPSPI_FlushFifo(spi, true, true);
    LPSPI_ClearStatusFlags(spi, kLPSPI_AllStatusFlag);

    /* Enable TX/RX */
    LPSPI_Enable(spi, true);

    /* Command has command phase */
    if (msg->instruction.qspi_lines != 0)
    {
        /* QSPI lines either 1, or 2, or 4, convert to 0, 1, 2 respectfully */
        uint8_t cmd_width = msg->instruction.qspi_lines >> 1U;

        spi->TCR = tcr_base | LPSPI_TCR_WIDTH(cmd_width) | LPSPI_TCR_RXMSK(1);

        /* Fixed 1-byte command */
        LPSPI_WriteData(spi, msg->instruction.content);

        /* Wait for FIFO empty... */
        while (LPSPI_GetTxFifoCount(spi))
        {
            /* -- */
        }
    }

    /* Command has modifier phase */
    if (msg->address.size || msg->alternate_bytes.size)
    {
        uint8_t addr_width = msg->address.qspi_lines >> 1U;

        /* Insert address bytes */
        for (uint8_t i = 0; i < msg->address.size; i++)
        {
            ca_buf[i] = (msg->address.content >> (((msg->address.size - 1) - i) * 8U)) & 0xFFU;
        }

        /* Insert mode bytes after address */
        for (uint8_t i = 0; i < msg->alternate_bytes.size; i++)
        {
            ca_buf[msg->address.size + i] =
                (msg->alternate_bytes.content >> (((msg->alternate_bytes.size - 1) - i) * 8U)) & 0xFFU;
        }

        spi->TCR = tcr_base | LPSPI_TCR_WIDTH(addr_width) | LPSPI_TCR_RXMSK(1);

        for (uint8_t i = 0; i < (msg->address.size + msg->alternate_bytes.size); i++)
        {
            LPSPI_WriteData(spi, ca_buf[i]);
        }

        /* Wait for FIFO empty... */
        while (LPSPI_GetTxFifoCount(spi))
        {
            /* -- */
        }
    }

    /* Command has dummy cycles */
    if (msg->dummy_cycles)
    {
        /* Use 4 wire mode for maximum flexibility. */
        uint8_t dummy_bytes = msg->dummy_cycles / 2;

        /* 1 byte equals 2 clocks */
        for (uint8_t i = 0; i < dummy_bytes; i++)
        {
            spi->TCR = tcr_base | LPSPI_TCR_WIDTH(2U) | LPSPI_TCR_TXMSK(1);

            while (LPSPI_GetRxFifoCount(spi) == 0)
            {
                /* Wait for RX FIFO */
            }

            LPSPI_ReadData(spi);
        }
    }

    /* Command has data phase */
    /* TODO: DMA should be used here. */

    if (msg->parent.length)
    {
        uint8_t data_width = msg->qspi_data_lines >> 1U;

        if (msg->parent.send_buf == NULL)
        {
            /* Read */
            uint8_t *recv_buf = msg->parent.recv_buf;

            RT_ASSERT(recv_buf);

            for (uint32_t i = 0; i < msg->parent.length; i++)
            {
                spi->TCR = tcr_base | LPSPI_TCR_WIDTH(data_width) | LPSPI_TCR_TXMSK(1);

                while (LPSPI_GetRxFifoCount(spi) == 0)
                {
                    /* Wait for RX FIFO */
                }

                recv_buf[i] = LPSPI_ReadData(spi);
            }
        }
        else
        {
            /* Write */
            const uint8_t *send_buf = msg->parent.send_buf;

            for (uint32_t i = 0; i < msg->parent.length; i++)
            {
                spi->TCR = tcr_base | LPSPI_TCR_WIDTH(data_width) | LPSPI_TCR_RXMSK(1);

                while (LPSPI_GetTxFifoCount(spi) == tx_fifo_size)
                {
                    /* Wait for TX FIFO  */
                }

                LPSPI_WriteData(spi, send_buf[i]);
            }

            /* Wait for write complete before de-asserting CS */
            while (LPSPI_GetTxFifoCount(spi) != 0)
            {
                /* -- */
            }
        }
    }

    spi->TCR = tcr_base & ~(LPSPI_TCR_CONTC_MASK);


    return 0;
}

rt_err_t
rt_hw_qspi_device_attach(const char *bus_name, const char *device_name, rt_base_t cs_pin, rt_uint8_t data_line_width,
                         void (*enter_qspi_mode)(), void (*exit_qspi_mode)())
{
    struct rt_qspi_device *qspi_device = RT_NULL;
    rt_err_t result = RT_EOK;

    RT_ASSERT(bus_name != RT_NULL);
    RT_ASSERT(device_name != RT_NULL);
    RT_ASSERT(data_line_width == 1 || data_line_width == 2 || data_line_width == 4);

    qspi_device = (struct rt_qspi_device *) rt_malloc(sizeof(struct rt_qspi_device));
    if (qspi_device == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto __exit;
    }

    qspi_device->enter_qspi_mode = enter_qspi_mode;
    qspi_device->exit_qspi_mode = exit_qspi_mode;
    qspi_device->config.qspi_dl_width = data_line_width;

#ifdef BSP_QSPI_USING_SOFTCS
    result = rt_spi_bus_attach_device_cspin(&qspi_device->parent, device_name, bus_name, cs_pin, RT_NULL);
#else
    result = rt_spi_bus_attach_device_cspin(&qspi_device->parent, device_name, bus_name, PIN_NONE, RT_NULL);
#endif /* BSP_QSPI_USING_SOFTCS */

__exit:
    if (result != RT_EOK)
    {
        if (qspi_device)
        {
            rt_free(qspi_device);
        }
    }

    return result;
}

static const struct rt_spi_ops mcx_qspi_ops =
{
    .configure = mcx_qspi_configure,
    .xfer = mcx_qspi_xfer,
};

static int mcx_qspi_bus_init(void)
{
    rt_err_t ret;

    char name_buf[10];

    for (unsigned int i = 0; i < ARRAY_SIZE(mcx_qspi_instances); i++)
    {
        mcx_qspi_list[i].spi_instance = mcx_qspi_instances[i];
        mcx_qspi_list[i].input_freq = CLOCK_GetLPFlexCommClkFreq(i);
        mcx_qspi_list[i].spi_bus.parent.user_data = &mcx_qspi_list[i];
        mcx_qspi_list[i].initialized = false;

        rt_snprintf(name_buf, sizeof(name_buf), "qspi%d", i);

        ret = rt_qspi_bus_register(&mcx_qspi_list[i].spi_bus, name_buf, &mcx_qspi_ops);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }


    return 0;
}

INIT_DEVICE_EXPORT(mcx_qspi_bus_init);

#endif