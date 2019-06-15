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
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#define DBG_ENABLE
#define DBG_SECTION_NAME "AT24CXX"
#define DBG_LEVEL DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#ifdef PKG_USING_AT24CXX
#include "at24cxx.h"

/**
 * This function read one byte from specific position
 *
 * @file   at24cxx.c
 * @param  client      the client of i2c bus
 * @param  readAddr    the position to read
 * @return             the data you have read
 */
static rt_uint8_t at24cxx_read_one_byte(struct rt_i2c_client *client, rt_uint8_t readAddr)
{
    rt_uint8_t read_data;
    rt_uint8_t read_address;
    struct rt_i2c_msg msgs[2];

    read_address = readAddr;
    msgs[0].addr  = client->client_addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = &read_address;
    msgs[0].len   = 1;

    msgs[1].addr  = client->client_addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf   = &read_data;
    msgs[1].len   = 1;

    rt_i2c_transfer(client->bus, msgs, 2);

    return read_data;
}

/**
 * This function write one byte from specific position
 *
 * @file   at24cxx.c
 * @param  client      the client of i2c bus
 * @param  writeAddr   the position to write
 * @param  dataToWrite the data to write
 * @return             RT_EOK/RT_ERROR
 */
static rt_err_t at24cxx_write_one_byte(struct rt_i2c_client *client, rt_uint8_t writeAddr, rt_uint8_t dataToWrite)
{
    rt_uint8_t write_buf[2];
    struct rt_i2c_msg msgs;

    write_buf[0] = writeAddr;
    write_buf[1] = dataToWrite;
    msgs.addr  = client->client_addr;
    msgs.flags = RT_I2C_WR;
    msgs.buf   = write_buf;
    msgs.len   = 2;

    if (rt_i2c_transfer(client->bus, &msgs, 1) == 1)
    {
        return RT_EOK;
    }
    else
    {
        return -RT_ERROR;
    }
}

static rt_err_t at24cxx_check(struct rt_i2c_client *dev)
{
    uint8_t temp;
    RT_ASSERT(dev);

    temp = at24cxx_read_one_byte(dev, 255);

    if (temp == 0x55)
    {
        return RT_EOK;
    }
    else
    {
        at24cxx_write_one_byte(dev, 255, 0x55);
        temp = at24cxx_read_one_byte(dev, 255);

        if (temp == 0x55)
            return RT_EOK;
    }

    return -RT_ERROR;
}

rt_err_t at24cxx_read(struct rt_i2c_client *dev, uint8_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
    RT_ASSERT(dev);

    while (NumToRead)
    {
        *pBuffer++ = at24cxx_read_one_byte(dev, ReadAddr++);
        NumToRead--;
    }

    return RT_EOK;
}

/**
 * This function write the specific numbers of data to the specific position
 *
 * @file   at24cxx.c
 * @param  bus         the name of at24cxx device
 * @param  WriteAddr   the start position to write
 * @param  pBuffer     the data need to write
 * @param  NumToWrite  the num of write data
 * @return RT_EOK      write ok.at24cxx_device_t dev
 */
rt_err_t at24cxx_write(struct rt_i2c_client *dev, uint8_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
    uint8_t i = 0;
    RT_ASSERT(dev);

    while (1)
    {
        if (at24cxx_write_one_byte(dev, WriteAddr, pBuffer[i]) != RT_EOK)
        {
            rt_thread_mdelay(1);
        }
        else
        {
            WriteAddr++;
            i++;
        }
        if (WriteAddr == NumToWrite)
        {
            break;
        }

    }

    return RT_EOK;
}
/**
 * This function initializes at24cxx registered device driver
 *
 * @param dev the name of at24cxx device
 *
 * @return the at24cxx device.
 */
struct rt_i2c_client *at24cxx_init(const char *i2c_bus_name)
{
    struct rt_i2c_client *i2c_client;

    RT_ASSERT(i2c_bus_name);

    i2c_client = rt_calloc(1, sizeof(struct rt_i2c_client));
    if (i2c_client == RT_NULL)
    {
        LOG_E("Can't allocate memory for at24cxx device on '%s' ", i2c_bus_name);
        return RT_NULL;
    }

    i2c_client->bus = rt_i2c_bus_device_find(i2c_bus_name);
    if (i2c_client->bus == RT_NULL)
    {
        LOG_E("Can't find at24cxx device on '%s' ", i2c_bus_name);
        rt_free(i2c_client);
        return RT_NULL;
    }
    i2c_client->client_addr = AT24CXX_ADDR;

    return i2c_client;
}

/**
 * This function releases memory and deletes mutex lock
 *
 * @param dev the pointer of device driver structure
 */
void at24cxx_deinit(struct rt_i2c_client *dev)
{
    RT_ASSERT(dev);

    rt_free(dev);
}

static rt_uint8_t TEST_BUFFER[] = "Welcom To RT-Thread";
#define SIZE sizeof(TEST_BUFFER)

static void at24cxx(int argc, char *argv[])
{
    static struct rt_i2c_client *dev = RT_NULL;

    if (argc > 1)
    {
        if (!strcmp(argv[1], "probe"))
        {
            if (argc > 2)
            {
                /* initialize the sensor when first probe */
                if (!dev || strcmp(dev->bus->parent.parent.name, argv[2]))
                {
                    /* deinit the old device */
                    if (dev)
                    {
                        at24cxx_deinit(dev);
                    }
                    dev = at24cxx_init(argv[2]);
                }
            }
            else
            {
                rt_kprintf("at24cxx probe <dev_name>   - probe sensor by given name\n");
            }
        }
        else if (!strcmp(argv[1], "read"))
        {
            if (dev)
            {
                uint8_t testbuffer[50];

                /* read the eeprom data */
                at24cxx_read(dev, 0, testbuffer, SIZE);

                rt_kprintf("read at24cxx : %s\n", testbuffer);

            }
            else
            {
                rt_kprintf("Please using 'at24cxx probe <dev_name>' first\n");
            }
        }
        else if (!strcmp(argv[1], "write"))
        {
            at24cxx_write(dev, 0, TEST_BUFFER, SIZE);
            rt_kprintf("write ok\n");
        }
        else if (!strcmp(argv[1], "check"))
        {
            if (at24cxx_check(dev) != RT_EOK)
            {
                rt_kprintf("check faild \n");
            }
            rt_kprintf("check success\n");
        }
        else
        {
            rt_kprintf("Unknown command. Please enter 'at24cxx0' for help\n");
        }
    }
    else
    {
        rt_kprintf("Usage:\n");
        rt_kprintf("at24cxx probe <dev_name>   - probe eeprom by given name\n");
        rt_kprintf("at24cxx check              - check eeprom at24cxx \n");
        rt_kprintf("at24cxx read               - read eeprom at24cxx data\n");
        rt_kprintf("at24cxx write              - write eeprom at24cxx data\n");

    }
}
MSH_CMD_EXPORT(at24cxx, at24cxx eeprom function);

#endif
