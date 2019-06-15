/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-13     XiaojieFan   the first version
 * 2019-06-14     tyustli      port to rt-thread rt_i2c_client
 */

#ifndef __AT24CXX_H__
#define __AT24CXX_H__

#include <rthw.h>
#include <rtthread.h>

#include <rthw.h>
#include <rtdevice.h>

#define AT24C01     127
#define AT24C02     255
#define AT24C04     511
#define AT24C08     1023
#define AT24C16     2047
#define AT24C32     4095
#define AT24C64     8191
#define AT24C128    16383
#define AT24C256    32767

#define EE_TYPE AT24C02
#define AT24CXX_ADDR 0x50

rt_err_t at24cxx_read(struct rt_i2c_client *dev, uint8_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);
rt_err_t at24cxx_write(struct rt_i2c_client *dev, uint8_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);
struct rt_i2c_client *at24cxx_init(const char *i2c_bus_name);

#endif
