/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-13     XiaojieFan   the first version
 * 2019-12-04     RenMing      ADD PAGE WRITE and input address can be selected
 * 2022-10-11     GuangweiRen  Delay 2ms after writing one byte
 * 2026-06-10     wdfk-prog    Reconstructing the core API and error contracts for reading and writing 
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include <string.h>
#include <stdlib.h>

#define DBG_ENABLE
#define DBG_SECTION_NAME "at24xx"
#define DBG_LEVEL DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#include "at24cxx.h"

#ifdef PKG_USING_AT24CXX
#define AT24CXX_ADDR (0xA0 >> 1) // A0 A1 A2 connect GND

/**
 * @brief Validate an EEPROM physical address range.
 *
 * @param addr EEPROM physical start address.
 * @param length Number of bytes in the range.
 *
 * @return RT_EOK if the range is valid, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_check_mem_range(uint32_t addr, uint32_t length)
{
    uint32_t max_addr = (uint32_t)AT24CXX_MAX_MEM_ADDRESS;

    if ((length == 0U) || (addr >= max_addr) || (length > (max_addr - addr)))
    {
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Validate AT24CXX address input bits for the selected EEPROM type.
 *
 * @param AddrInput Address input bits or bank select value.
 *
 * @return RT_EOK if the address input is valid, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_check_addr_input(uint8_t AddrInput)
{
    if (AddrInput > 7U)
    {
        return RT_ERROR;
    }

#if (PKG_AT24CXX_EE_TYPE == AT24C04)
    if (AddrInput > 3U)
    {
        return RT_ERROR;
    }
#elif (PKG_AT24CXX_EE_TYPE == AT24C08)
    if (AddrInput > 1U)
    {
        return RT_ERROR;
    }
#elif (PKG_AT24CXX_EE_TYPE == AT24C16)
    if (AddrInput != 0U)
    {
        return RT_ERROR;
    }
#endif /* (PKG_AT24CXX_EE_TYPE == AT24C04) */

    return RT_EOK;
}

/**
 * @brief Read bytes from the current AT24CXX EEPROM address pointer.
 *
 * @param dev AT24CXX device handle.
 * @param len Number of bytes to read.
 * @param buf Buffer used to store read bytes.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_read_regs(at24cxx_device_t dev, rt_uint8_t len, rt_uint8_t *buf)
{
    struct rt_i2c_msg msgs;

    msgs.addr = AT24CXX_ADDR | dev->AddrInput;
    msgs.flags = RT_I2C_RD;
    msgs.buf = buf;
    msgs.len = len;

    if (rt_i2c_transfer(dev->i2c, &msgs, 1) != 1)
    {
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Read one byte from a specified EEPROM physical address.
 *
 * @param dev AT24CXX device handle.
 * @param readAddr EEPROM physical address to read.
 * @param data Pointer used to store the read byte.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_read_one_byte(at24cxx_device_t dev, uint32_t readAddr, uint8_t *data)
{
    rt_uint8_t addr_buf[2];

#if (PKG_AT24CXX_EE_TYPE > AT24C16)
    addr_buf[0] = (uint8_t)(readAddr >> 8);
    addr_buf[1] = (uint8_t)readAddr;
    if (rt_i2c_master_send(dev->i2c, AT24CXX_ADDR | dev->AddrInput, RT_I2C_WR, addr_buf, 2) != 2)
#else
    addr_buf[0] = (uint8_t)readAddr;
    if (rt_i2c_master_send(dev->i2c, AT24CXX_ADDR | dev->AddrInput, RT_I2C_WR, addr_buf, 1) != 1)
#endif
    {
        return RT_ERROR;
    }

    return at24cxx_read_regs(dev, 1, data);
}

/**
 * @brief Write one byte to a specified EEPROM physical address.
 *
 * @param dev AT24CXX device handle.
 * @param writeAddr EEPROM physical address to write.
 * @param dataToWrite Byte value to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_write_one_byte(at24cxx_device_t dev, uint32_t writeAddr, uint8_t dataToWrite)
{
    rt_uint8_t buf[3];

#if (PKG_AT24CXX_EE_TYPE > AT24C16)
    buf[0] = (uint8_t)(writeAddr >> 8);
    buf[1] = (uint8_t)writeAddr;
    buf[2] = dataToWrite;
    if (rt_i2c_master_send(dev->i2c, AT24CXX_ADDR | dev->AddrInput, RT_I2C_WR, buf, 3) != 3)
#else
    buf[0] = (uint8_t)writeAddr;
    buf[1] = dataToWrite;

    if (rt_i2c_master_send(dev->i2c, AT24CXX_ADDR | dev->AddrInput, RT_I2C_WR, buf, 2) != 2)
#endif
    {
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Read one contiguous EEPROM page fragment.
 *
 * @param dev AT24CXX device handle.
 * @param readAddr EEPROM physical start address.
 * @param pBuffer Read buffer.
 * @param numToRead Number of bytes to read.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_read_page(at24cxx_device_t dev, uint32_t readAddr, uint8_t *pBuffer, uint16_t numToRead)
{
    struct rt_i2c_msg msgs[2];
    uint8_t AddrBuf[2];

    msgs[0].addr = AT24CXX_ADDR | dev->AddrInput;
    msgs[0].flags = RT_I2C_WR;

#if (PKG_AT24CXX_EE_TYPE > AT24C16)
    AddrBuf[0] = (uint8_t)(readAddr >> 8);
    AddrBuf[1] = (uint8_t)readAddr;
    msgs[0].buf = AddrBuf;
    msgs[0].len = 2;
#else
    AddrBuf[0] = (uint8_t)readAddr;
    msgs[0].buf = AddrBuf;
    msgs[0].len = 1;
#endif

    msgs[1].addr = AT24CXX_ADDR | dev->AddrInput;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = pBuffer;
    msgs[1].len = numToRead;

    if (rt_i2c_transfer(dev->i2c, msgs, 2) != 2)
    {
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Write one contiguous EEPROM page fragment.
 *
 * @param dev AT24CXX device handle.
 * @param writeAddr EEPROM physical start address.
 * @param pBuffer Write buffer.
 * @param numToWrite Number of bytes to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_write_page(at24cxx_device_t dev, uint32_t writeAddr, uint8_t *pBuffer, uint16_t numToWrite)
{
    struct rt_i2c_msg msgs[2];
    uint8_t AddrBuf[2];

    msgs[0].addr = AT24CXX_ADDR | dev->AddrInput;
    msgs[0].flags = RT_I2C_WR;

#if (PKG_AT24CXX_EE_TYPE > AT24C16)
    AddrBuf[0] = (uint8_t)(writeAddr >> 8);
    AddrBuf[1] = (uint8_t)writeAddr;
    msgs[0].buf = AddrBuf;
    msgs[0].len = 2;
#else
    AddrBuf[0] = (uint8_t)writeAddr;
    msgs[0].buf = AddrBuf;
    msgs[0].len = 1;
#endif

    msgs[1].addr = AT24CXX_ADDR | dev->AddrInput;
    msgs[1].flags = RT_I2C_WR | RT_I2C_NO_START;
    msgs[1].buf = pBuffer;
    msgs[1].len = numToWrite;

    if (rt_i2c_transfer(dev->i2c, msgs, 2) != 2)
    {
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Check whether the AT24CXX EEPROM is accessible with a mutex timeout.
 *
 * This function reads the last EEPROM byte. If it is not 0x55, the function
 * writes 0x55 to that byte and reads it back for verification.
 *
 * @param dev AT24CXX device handle.
 * @param timeout Maximum ticks to wait for the device mutex.
 *
 * @return RT_EOK if the EEPROM is accessible, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_check_timeout(at24cxx_device_t dev, rt_int32_t timeout)
{
    uint8_t temp;
    uint32_t check_addr = (uint32_t)AT24CXX_MAX_MEM_ADDRESS - 1U;
    rt_err_t result;

    RT_ASSERT(dev);

    result = rt_mutex_take(dev->lock, timeout);
    if (result != RT_EOK)
    {
        LOG_E("The at24cxx could not get mutex within timeout=%d", timeout);
        return result;
    }

    result = at24cxx_read_one_byte(dev, check_addr, &temp);
    if (result != RT_EOK)
    {
        goto exit;
    }

    if (temp == 0x55U)
    {
        result = RT_EOK;
        goto exit;
    }

    result = at24cxx_write_one_byte(dev, check_addr, 0x55U);
    if (result != RT_EOK)
    {
        goto exit;
    }

    rt_thread_mdelay(EE_TWR);

    result = at24cxx_read_one_byte(dev, check_addr, &temp);
    if (result != RT_EOK)
    {
        goto exit;
    }

    result = (temp == 0x55U) ? RT_EOK : RT_ERROR;

exit:
    rt_mutex_release(dev->lock);
    return result;
}

/**
 * @brief Check whether the AT24CXX EEPROM is accessible.
 *
 * This compatibility API keeps the legacy blocking behavior and waits forever
 * for the device mutex.
 *
 * @param dev AT24CXX device handle.
 *
 * @return RT_EOK if the EEPROM is accessible, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_check(at24cxx_device_t dev)
{
    return at24cxx_check_timeout(dev, RT_WAITING_FOREVER);
}

/**
 * @brief Read bytes from a specified EEPROM physical address with a mutex timeout.
 *
 * @param dev AT24CXX device handle.
 * @param ReadAddr EEPROM physical start address.
 * @param pBuffer Buffer used to store read bytes.
 * @param NumToRead Number of bytes to read.
 * @param timeout Maximum ticks to wait for the device mutex.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_read_timeout(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead, rt_int32_t timeout)
{
    rt_err_t result;

    RT_ASSERT(dev);

    if ((pBuffer == RT_NULL) || (at24cxx_check_mem_range(ReadAddr, NumToRead) != RT_EOK))
    {
        return RT_ERROR;
    }

    result = rt_mutex_take(dev->lock, timeout);
    if (result != RT_EOK)
    {
        LOG_E("The at24cxx could not get mutex within timeout=%d", timeout);
        return result;
    }

    while (NumToRead)
    {
        result = at24cxx_read_one_byte(dev, ReadAddr, pBuffer);
        if (result != RT_EOK)
        {
            break;
        }

        ReadAddr++;
        pBuffer++;
        NumToRead--;
    }

    rt_mutex_release(dev->lock);
    return result;
}

/**
 * @brief Read bytes from a specified EEPROM physical address.
 *
 * This compatibility API keeps the legacy blocking behavior and waits forever
 * for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param ReadAddr EEPROM physical start address.
 * @param pBuffer Buffer used to store read bytes.
 * @param NumToRead Number of bytes to read.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_read(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
    return at24cxx_read_timeout(dev, ReadAddr, pBuffer, NumToRead, RT_WAITING_FOREVER);
}

/**
 * @brief Page-read bytes from a specified EEPROM physical address with a mutex timeout.
 *
 * @param dev AT24CXX device handle.
 * @param ReadAddr EEPROM physical start address.
 * @param pBuffer Buffer used to store read bytes.
 * @param NumToRead Number of bytes to read.
 * @param timeout Maximum ticks to wait for the device mutex.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_page_read_timeout(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead, rt_int32_t timeout)
{
    rt_err_t result;
    uint16_t pageReadSize;

    RT_ASSERT(dev);

    if ((pBuffer == RT_NULL) || (at24cxx_check_mem_range(ReadAddr, NumToRead) != RT_EOK))
    {
        return RT_ERROR;
    }

    pageReadSize = AT24CXX_PAGE_BYTE - ReadAddr % AT24CXX_PAGE_BYTE;

    result = rt_mutex_take(dev->lock, timeout);
    if (result != RT_EOK)
    {
        LOG_E("The at24cxx could not get mutex within timeout=%d", timeout);
        return result;
    }

    while (NumToRead)
    {
        uint16_t chunk;

        chunk = (NumToRead > pageReadSize) ? pageReadSize : NumToRead;
        result = at24cxx_read_page(dev, ReadAddr, pBuffer, chunk);
        if (result != RT_EOK)
        {
            break;
        }

        ReadAddr += chunk;
        pBuffer += chunk;
        NumToRead -= chunk;
        pageReadSize = AT24CXX_PAGE_BYTE;
    }

    rt_mutex_release(dev->lock);
    return result;
}

/**
 * @brief Page-read bytes from a specified EEPROM physical address.
 *
 * This compatibility API keeps the legacy blocking behavior and waits forever
 * for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param ReadAddr EEPROM physical start address.
 * @param pBuffer Buffer used to store read bytes.
 * @param NumToRead Number of bytes to read.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_page_read(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
    return at24cxx_page_read_timeout(dev, ReadAddr, pBuffer, NumToRead, RT_WAITING_FOREVER);
}

/**
 * @brief Write bytes to a specified EEPROM physical address with a mutex timeout.
 *
 * @param dev AT24CXX device handle.
 * @param WriteAddr EEPROM physical start address.
 * @param pBuffer Buffer that stores bytes to write.
 * @param NumToWrite Number of bytes to write.
 * @param timeout Maximum ticks to wait for the device mutex.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_write_timeout(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite, rt_int32_t timeout)
{
    uint16_t i = 0U;
    rt_err_t result;

    RT_ASSERT(dev);

    if ((pBuffer == RT_NULL) || (at24cxx_check_mem_range(WriteAddr, NumToWrite) != RT_EOK))
    {
        return RT_ERROR;
    }

    result = rt_mutex_take(dev->lock, timeout);
    if (result != RT_EOK)
    {
        LOG_E("The at24cxx could not get mutex within timeout=%d", timeout);
        return result;
    }

    while (i < NumToWrite)
    {
        result = at24cxx_write_one_byte(dev, WriteAddr, pBuffer[i]);
        if (result != RT_EOK)
        {
            break;
        }

        rt_thread_mdelay(2);
        WriteAddr++;
        i++;

        if (i < NumToWrite)
        {
            rt_thread_mdelay(EE_TWR);
        }
    }

    rt_mutex_release(dev->lock);
    return result;
}

/**
 * @brief Write bytes to a specified EEPROM physical address.
 *
 * This compatibility API keeps the legacy blocking behavior and waits forever
 * for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param WriteAddr EEPROM physical start address.
 * @param pBuffer Buffer that stores bytes to write.
 * @param NumToWrite Number of bytes to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_write(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
    return at24cxx_write_timeout(dev, WriteAddr, pBuffer, NumToWrite, RT_WAITING_FOREVER);
}

/**
 * @brief Page-write bytes to a specified EEPROM physical address with a mutex timeout.
 *
 * @param dev AT24CXX device handle.
 * @param WriteAddr EEPROM physical start address.
 * @param pBuffer Buffer that stores bytes to write.
 * @param NumToWrite Number of bytes to write.
 * @param timeout Maximum ticks to wait for the device mutex.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_page_write_timeout(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite, rt_int32_t timeout)
{
    rt_err_t result;
    uint16_t pageWriteSize;

    RT_ASSERT(dev);

    if ((pBuffer == RT_NULL) || (at24cxx_check_mem_range(WriteAddr, NumToWrite) != RT_EOK))
    {
        return RT_ERROR;
    }

    pageWriteSize = AT24CXX_PAGE_BYTE - WriteAddr % AT24CXX_PAGE_BYTE;

    result = rt_mutex_take(dev->lock, timeout);
    if (result != RT_EOK)
    {
        LOG_E("The at24cxx could not get mutex within timeout=%d", timeout);
        return result;
    }

    while (NumToWrite)
    {
        uint16_t chunk;

        chunk = (NumToWrite > pageWriteSize) ? pageWriteSize : NumToWrite;
        result = at24cxx_write_page(dev, WriteAddr, pBuffer, chunk);
        if (result != RT_EOK)
        {
            break;
        }

        rt_thread_mdelay(EE_TWR);
        WriteAddr += chunk;
        pBuffer += chunk;
        NumToWrite -= chunk;
        pageWriteSize = AT24CXX_PAGE_BYTE;
    }

    rt_mutex_release(dev->lock);
    return result;
}

/**
 * @brief Page-write bytes to a specified EEPROM physical address.
 *
 * This compatibility API keeps the legacy blocking behavior and waits forever
 * for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param WriteAddr EEPROM physical start address.
 * @param pBuffer Buffer that stores bytes to write.
 * @param NumToWrite Number of bytes to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
rt_err_t at24cxx_page_write(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
    return at24cxx_page_write_timeout(dev, WriteAddr, pBuffer, NumToWrite, RT_WAITING_FOREVER);
}

/**
 * @brief Bind a caller-provided AT24CXX device object and mutex.
 *
 * This function does not allocate the AT24CXX device object and does not
 * create, initialize, detach, or delete the supplied mutex. The caller owns
 * both @p dev and @p lock. The mutex must be initialized before this function
 * is called and must remain valid until all operations using @p dev stop.
 *
 * @param dev Caller-provided AT24CXX device object.
 * @param lock Caller-provided initialized mutex handle.
 * @param i2c_bus_name I2C bus device name.
 * @param AddrInput Address input bits or bank select value.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
rt_err_t at24cxx_init_device(at24cxx_device_t dev, rt_mutex_t lock, const char *i2c_bus_name, uint8_t AddrInput)
{
    struct rt_i2c_bus_device *i2c;

    if ((dev == RT_NULL) || (lock == RT_NULL) || (i2c_bus_name == RT_NULL))
    {
        return RT_ERROR;
    }

    if (at24cxx_check_addr_input(AddrInput) != RT_EOK)
    {
        LOG_E("The AddrInput is invalid");
        return RT_ERROR;
    }

    i2c = rt_i2c_bus_device_find(i2c_bus_name);
    if (i2c == RT_NULL)
    {
        LOG_E("Can't find at24cxx device on '%s' ", i2c_bus_name);
        return RT_ERROR;
    }

    memset(dev, 0, sizeof(*dev));
    dev->i2c = i2c;
    dev->lock = lock;
    dev->AddrInput = AddrInput;

    return RT_EOK;
}

/**
 * @brief Initialize an AT24CXX device on the specified I2C bus.
 *
 * This compatibility API allocates the device object and creates the mutex
 * internally. Use at24cxx_init_device() when both objects must be provided by
 * the caller.
 *
 * @param i2c_bus_name I2C bus device name.
 * @param AddrInput Address input bits or bank select value.
 *
 * @return AT24CXX device handle on success, otherwise RT_NULL.
 */
at24cxx_device_t at24cxx_init(const char *i2c_bus_name, uint8_t AddrInput)
{
    at24cxx_device_t dev;
    rt_mutex_t lock;

    RT_ASSERT(i2c_bus_name);

    lock = rt_mutex_create("mutex_at24cxx", RT_IPC_FLAG_FIFO);
    if (lock == RT_NULL)
    {
        LOG_E("Can't create mutex for at24cxx device on '%s' ", i2c_bus_name);
        return RT_NULL;
    }

    dev = rt_calloc(1, sizeof(struct at24cxx_device));
    if (dev == RT_NULL)
    {
        LOG_E("Can't allocate memory for at24cxx device on '%s' ", i2c_bus_name);
        rt_mutex_delete(lock);
        return RT_NULL;
    }

    if (at24cxx_init_device(dev, lock, i2c_bus_name, AddrInput) != RT_EOK)
    {
        rt_mutex_delete(lock);
        rt_free(dev);
        return RT_NULL;
    }

    return dev;
}

/**
 * @brief Deinitialize a caller-provided AT24CXX device object.
 *
 * This function only clears the device binding created by
 * at24cxx_init_device(). It does not detach, delete, or otherwise release the
 * caller-provided mutex, and it does not free @p dev.
 *
 * @param dev Caller-provided AT24CXX device handle to unbind.
 */
void at24cxx_deinit_device(at24cxx_device_t dev)
{
    RT_ASSERT(dev);

    memset(dev, 0, sizeof(*dev));
}

/**
 * @brief Deinitialize a dynamically allocated AT24CXX device.
 *
 * This compatibility API must be used only for device handles returned by
 * at24cxx_init(). It deletes the internally created mutex and frees @p dev.
 * Use at24cxx_deinit_device() for caller-provided device objects initialized
 * by at24cxx_init_device().
 *
 * @param dev Dynamically allocated AT24CXX device handle to release.
 */
void at24cxx_deinit(at24cxx_device_t dev)
{
    rt_mutex_t lock;

    RT_ASSERT(dev);

    lock = dev->lock;
    memset(dev, 0, sizeof(*dev));

    if (lock != RT_NULL)
    {
        rt_mutex_delete(lock);
    }

    rt_free(dev);
}

#ifdef PKG_AT24CXX_FINSH
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
                    dev = at24cxx_init(argv[2], atoi(argv[3]));
                }
            }
            else
            {
                rt_kprintf("at24cxx probe <dev_name> <AddrInput> - probe sensor by given name\n");
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
#endif /* PKG_AT24CXX_FINSH */
#endif /* PKG_USING_AT24CXX */
