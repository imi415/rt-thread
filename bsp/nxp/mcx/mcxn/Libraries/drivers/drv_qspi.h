#ifndef __DRV_QSPI_H__
#define __DRV_QSPI_H__

#include <rtthread.h>

rt_err_t rt_hw_qspi_device_attach(const char *bus_name, const char *device_name, rt_base_t cs_pin, rt_uint8_t data_line_width, void (*enter_qspi_mode)(), void (*exit_qspi_mode)());

#endif //__DRV_QSPI_H__
