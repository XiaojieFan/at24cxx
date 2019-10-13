/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-13     XiaojieFan   the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>


#include <string.h>

#define DBG_ENABLE
#define DBG_SECTION_NAME "AHT10"
#define DBG_LEVEL DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#include "at24cxx.h"

#ifdef PKG_USING_AT24CXX
#define AT24CXX_ADDR 0x50                      //A0 A1 A2 connect GND

static rt_err_t read_regs(struct rt_i2c_bus_device *bus, rt_uint8_t len, rt_uint8_t *buf)
{
    struct rt_i2c_msg msgs;

    msgs.addr = AT24CXX_ADDR;
    msgs.flags = RT_I2C_RD;
    msgs.buf = buf;
    msgs.len = len;

    if (rt_i2c_transfer(bus, &msgs, 1) == 1)
    {
        return RT_EOK;
    }
    else
    {
        return -RT_ERROR;
    }
}
uint8_t at24cxx_read_one_byte(struct rt_i2c_bus_device *bus, uint16_t readAddr)
{
    rt_uint8_t buf[2];
    rt_uint8_t temp;
#if	(EE_TYPE > AT24C16)  
    buf[0] = (uint8_t)(readAddr>>8);	
	buf[1] = (uint8_t)readAddr;
    if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 2) == 0) 
#else
    buf[0] = readAddr;
    if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 1) == 0)
#endif        
    {
        return -RT_ERROR;
    }
    read_regs(bus, 1, &temp);
    return temp;
}

rt_err_t at24cxx_write_one_byte(struct rt_i2c_bus_device *bus, uint16_t writeAddr, uint8_t dataToWrite)
{
    rt_uint8_t buf[3];
#if	(EE_TYPE > AT24C16)      
    buf[0] = (uint8_t)(writeAddr>>8);	
	buf[1] = (uint8_t)writeAddr;
    buf[2] = dataToWrite;
    if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 3) == 3)    
#else    
    buf[0] = writeAddr; //cmd
    buf[1] = dataToWrite;
    //buf[2] = data[1];

    if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 2) == 2)
#endif        
        return RT_EOK;
    else
        return -RT_ERROR;

}

rt_err_t at24cxx_check(at24cxx_device_t dev)
{
    uint8_t temp;
    rt_err_t result;
    RT_ASSERT(dev);

    temp = at24cxx_read_one_byte(dev->i2c, 255);
    if (temp == 0x55) return RT_EOK;
    else
    {
        at24cxx_write_one_byte(dev->i2c, 255, 0x55);
        temp = at24cxx_read_one_byte(dev->i2c, 255);
        if (temp == 0x55) return RT_EOK;
    }
    return RT_ERROR;
}

/**
 * This function read the specific numbers of data to the specific position
 *
 * @param bus the name of at24cxx device
 * @param ReadAddr the start position to read
 * @param pBuffer  the read data store position
 * @param NumToRead
 * @return RT_EOK  write ok.
 */
rt_err_t at24cxx_read(at24cxx_device_t dev, uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
    rt_err_t result;
    RT_ASSERT(dev);
    result = rt_mutex_take(dev->lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        while (NumToRead)
        {
            *pBuffer++ = at24cxx_read_one_byte(dev->i2c, ReadAddr++);
            NumToRead--;
        }
    }
    else
    {
        LOG_E("The at24cxx could not respond  at this time. Please try again");
    }
    rt_mutex_release(dev->lock);

    return RT_EOK;
}

/**
 * This function write the specific numbers of data to the specific position
 *
 * @param bus the name of at24cxx device
 * @param WriteAddr the start position to write
 * @param pBuffer  the data need to write
 * @param NumToWrite
 * @return RT_EOK  write ok.at24cxx_device_t dev
 */
rt_err_t at24cxx_write(at24cxx_device_t dev, uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
    uint16_t i = 0;
    rt_err_t result;
    RT_ASSERT(dev);
    result = rt_mutex_take(dev->lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        while (1) //NumToWrite--
        {
            if (at24cxx_write_one_byte(dev->i2c, WriteAddr, pBuffer[i]) != RT_EOK)
            {
                rt_thread_mdelay(1);
            }
            else
            {
                WriteAddr++;
                i++;
            }
            if (i == NumToWrite)
            {
                break;
            }

        }
    }
    else
    {
        LOG_E("The at24cxx could not respond  at this time. Please try again");
    }
    rt_mutex_release(dev->lock);

    return RT_EOK;
}
/**
 * This function initializes at24cxx registered device driver
 *
 * @param dev the name of at24cxx device
 *
 * @return the at24cxx device.
 */
at24cxx_device_t at24cxx_init(const char *i2c_bus_name)
{
    at24cxx_device_t dev;

    RT_ASSERT(i2c_bus_name);

    dev = rt_calloc(1, sizeof(struct at24cxx_device));
    if (dev == RT_NULL)
    {
        LOG_E("Can't allocate memory for at24cxx device on '%s' ", i2c_bus_name);
        return RT_NULL;
    }

    dev->i2c = rt_i2c_bus_device_find(i2c_bus_name);
    if (dev->i2c == RT_NULL)
    {
        LOG_E("Can't find at24cxx device on '%s' ", i2c_bus_name);
        rt_free(dev);
        return RT_NULL;
    }

    dev->lock = rt_mutex_create("mutex_at24cxx", RT_IPC_FLAG_FIFO);
    if (dev->lock == RT_NULL)
    {
        LOG_E("Can't create mutex for at24cxx device on '%s' ", i2c_bus_name);
        rt_free(dev);
        return RT_NULL;
    }

    return dev;
}

/**
 * This function releases memory and deletes mutex lock
 *
 * @param dev the pointer of device driver structure
 */
void at24cxx_deinit(at24cxx_device_t dev)
{
    RT_ASSERT(dev);

    rt_mutex_delete(dev->lock);

    rt_free(dev);
}

uint8_t TEST_BUFFER[] = "WELCOM TO RTT";
#define SIZE sizeof(TEST_BUFFER)

void at24cxx(int argc, char *argv[])
{
    static at24cxx_device_t dev = RT_NULL;

    if (argc > 1)
    {
        if (!strcmp(argv[1], "probe"))
        {
            if (argc > 2)
            {
                /* initialize the sensor when first probe */
                if (!dev || strcmp(dev->i2c->parent.parent.name, argv[2]))
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
            if (at24cxx_check(dev) == 1)
            {
                rt_kprintf("check faild \n");
            }
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
