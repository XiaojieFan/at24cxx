/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-13     XiaojieFan   the first version
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
#define AT24C512    65535

#define EE_TYPE AT24C02
struct at24cxx_device
{
    struct rt_i2c_bus_device *i2c;
    rt_mutex_t lock;
};
typedef struct at24cxx_device *at24cxx_device_t;
#endif
