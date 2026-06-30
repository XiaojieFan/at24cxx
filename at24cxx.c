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
 * 2026-06-10     wdfk-prog    Refactor Finsh/MSH debugging commands 
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
/** Number of EEPROM bytes printed per MSH dump line. */
#define AT24CXX_DUMP_LINE_BYTES 16U

/** Maximum EEPROM bytes written by one fill transaction. */
#define AT24CXX_FILL_CHUNK_BYTES 64U

#if (PKG_AT24CXX_EE_TYPE == AT24C01)
/** Human-readable EEPROM type name for MSH output. */
#define AT24CXX_EE_TYPE_NAME "AT24C01"
#elif (PKG_AT24CXX_EE_TYPE == AT24C02)
#define AT24CXX_EE_TYPE_NAME "AT24C02"
#elif (PKG_AT24CXX_EE_TYPE == AT24C04)
#define AT24CXX_EE_TYPE_NAME "AT24C04"
#elif (PKG_AT24CXX_EE_TYPE == AT24C08)
#define AT24CXX_EE_TYPE_NAME "AT24C08"
#elif (PKG_AT24CXX_EE_TYPE == AT24C16)
#define AT24CXX_EE_TYPE_NAME "AT24C16"
#elif (PKG_AT24CXX_EE_TYPE == AT24C32)
#define AT24CXX_EE_TYPE_NAME "AT24C32"
#elif (PKG_AT24CXX_EE_TYPE == AT24C64)
#define AT24CXX_EE_TYPE_NAME "AT24C64"
#elif (PKG_AT24CXX_EE_TYPE == AT24C128)
#define AT24CXX_EE_TYPE_NAME "AT24C128"
#elif (PKG_AT24CXX_EE_TYPE == AT24C256)
#define AT24CXX_EE_TYPE_NAME "AT24C256"
#elif (PKG_AT24CXX_EE_TYPE == AT24C512)
#define AT24CXX_EE_TYPE_NAME "AT24C512"
#else
#define AT24CXX_EE_TYPE_NAME "UNKNOWN"
#endif /* (PKG_AT24CXX_EE_TYPE == AT24C01) */

/**
 * @brief Print AT24CXX MSH usage.
 */
static void at24cxx_show_usage(void)
{
    rt_kprintf("Usage:\n");
    rt_kprintf("  at24cxx probe <dev_name> <AddrInput>\n");
    rt_kprintf("  at24cxx info\n");
    rt_kprintf("  at24cxx dump <addr> <len>\n");
    rt_kprintf("  at24cxx read <addr> <len>\n");
    rt_kprintf("  at24cxx write <addr> <byte0> [byte1] ...\n");
    rt_kprintf("  at24cxx fill <addr> <len> <byte>\n");
#ifdef PKG_AT24CXX_USING_TEST_CMD
    rt_kprintf("Test commands:\n");
    rt_kprintf("  at24cxx test_check\n");
    rt_kprintf("  at24cxx test_read\n");
    rt_kprintf("  at24cxx test_write\n");
#endif /* PKG_AT24CXX_USING_TEST_CMD */
    rt_kprintf("Examples:\n");
    rt_kprintf("  at24cxx probe i2c1 0\n");
    rt_kprintf("  at24cxx dump 0x1000 0x40\n");
    rt_kprintf("  at24cxx write 0x1017 0x00\n");
    rt_kprintf("  at24cxx fill 0x1000 0x20 0xff\n");
}

/**
 * @brief Check whether an AT24CXX device has been probed.
 *
 * @param dev AT24CXX device handle.
 *
 * @return RT_TRUE if the device is available, otherwise RT_FALSE.
 */
static rt_bool_t at24cxx_is_ready(at24cxx_device_t dev)
{
    if (dev == RT_NULL)
    {
        rt_kprintf("Please using 'at24cxx probe <dev_name> <AddrInput>' first\n");
        return RT_FALSE;
    }

    return RT_TRUE;
}

/**
 * @brief Parse an unsigned 32-bit integer from an MSH argument.
 *
 * @param text Input string. Decimal and 0x-prefixed hexadecimal are supported.
 * @param value Parsed value output.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_parse_u32(const char *text, uint32_t *value)
{
    char *endptr;
    unsigned long parsed;

    if ((text == RT_NULL) || (value == RT_NULL) ||
        (text[0] == '\0') || (text[0] == '-'))
    {
        return RT_ERROR;
    }

    parsed = strtoul(text, &endptr, 0);
    if ((endptr == text) || (*endptr != '\0'))
    {
        return RT_ERROR;
    }

    *value = (uint32_t)parsed;
    return RT_EOK;
}

/**
 * @brief Parse one EEPROM byte from an MSH argument.
 *
 * @param text Input string. Decimal and 0x-prefixed hexadecimal are supported.
 * @param value Parsed byte output.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_parse_u8(const char *text, uint8_t *value)
{
    uint32_t parsed;

    if ((value == RT_NULL) ||
        (at24cxx_parse_u32(text, &parsed) != RT_EOK) ||
        (parsed > 0xffU))
    {
        return RT_ERROR;
    }

    *value = (uint8_t)parsed;
    return RT_EOK;
}

/**
 * @brief Validate an EEPROM address range.
 *
 * @param addr EEPROM physical start address.
 * @param length Number of bytes in the range.
 *
 * @return RT_EOK if the range is valid, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_check_range(uint32_t addr, uint32_t length)
{
    if (at24cxx_check_mem_range(addr, length) != RT_EOK)
    {
        rt_kprintf("Range error: addr=0x%08x len=%u max=0x%08x\n",
                   (unsigned int)addr,
                   (unsigned int)length,
                   (unsigned int)AT24CXX_MAX_MEM_ADDRESS);
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Print current AT24CXX MSH device information.
 *
 * @param dev AT24CXX device handle.
 */
static void at24cxx_print_info(at24cxx_device_t dev)
{
    if (at24cxx_is_ready(dev) == RT_FALSE)
    {
        return;
    }

    rt_kprintf("AT24CXX info:\n");
    rt_kprintf("  bus        : %s\n", dev->i2c->parent.parent.name);
    rt_kprintf("  addr_input : %u\n", (unsigned int)dev->AddrInput);
    rt_kprintf("  type       : %s\n", AT24CXX_EE_TYPE_NAME);
    rt_kprintf("  size       : %u bytes\n",
               (unsigned int)AT24CXX_MAX_MEM_ADDRESS);
    rt_kprintf("  page       : %u bytes\n", (unsigned int)AT24CXX_PAGE_BYTE);
}

/**
 * @brief Dump raw bytes from the AT24CXX EEPROM.
 *
 * @param dev AT24CXX device handle.
 * @param addr EEPROM physical start address.
 * @param length Number of bytes to dump.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_dump_data(at24cxx_device_t dev,
                                  uint32_t addr,
                                  uint32_t length)
{
    uint8_t buf[AT24CXX_DUMP_LINE_BYTES];
    uint32_t remain;

    RT_ASSERT(dev);

    if (at24cxx_check_range(addr, length) != RT_EOK)
    {
        rt_kprintf("Usage: at24cxx dump <addr> <len>\n");
        rt_kprintf("Example: at24cxx dump 0x1000 0x40\n");
        return RT_ERROR;
    }

    rt_kprintf("AT24 dump: bus=%s addr_input=%u addr=0x%08x len=%u\n",
               dev->i2c->parent.parent.name,
               (unsigned int)dev->AddrInput,
               (unsigned int)addr,
               (unsigned int)length);

    remain = length;

    while (remain > 0U)
    {
        uint16_t chunk;
        uint16_t i;

        chunk = (remain > AT24CXX_DUMP_LINE_BYTES) ? AT24CXX_DUMP_LINE_BYTES : (uint16_t)remain;

        if (at24cxx_page_read(dev, addr, buf, chunk) != RT_EOK)
        {
            rt_kprintf("AT24 dump read failed at 0x%08x len=%u\n",
                       (unsigned int)addr,
                       (unsigned int)chunk);
            return RT_ERROR;
        }

        rt_kprintf("%08x:", (unsigned int)addr);

        for (i = 0U; i < chunk; i++)
        {
            rt_kprintf(" %02x", buf[i]);
        }

        rt_kprintf("\n");

        addr += chunk;
        remain -= chunk;
    }

    return RT_EOK;
}

/**
 * @brief Write raw bytes passed from MSH to the AT24CXX EEPROM.
 *
 * @param dev AT24CXX device handle.
 * @param addr EEPROM physical start address.
 * @param argc MSH argument count.
 * @param argv MSH argument vector.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_write_msh_bytes(at24cxx_device_t dev,
                                        uint32_t addr,
                                        int argc,
                                        char *argv[])
{
    uint8_t *buf;
    uint32_t length;
    uint32_t i;
    rt_err_t result;

    RT_ASSERT(dev);

    length = (uint32_t)(argc - 3);
    if (at24cxx_check_range(addr, length) != RT_EOK)
    {
        return RT_ERROR;
    }

    if (length > 0xffffU)
    {
        rt_kprintf("Write length is too large: %u\n", (unsigned int)length);
        return RT_ERROR;
    }

    buf = rt_calloc(length, sizeof(uint8_t));
    if (buf == RT_NULL)
    {
        rt_kprintf("No memory for write buffer, len=%u\n", (unsigned int)length);
        return RT_ERROR;
    }

    for (i = 0U; i < length; i++)
    {
        if (at24cxx_parse_u8(argv[i + 3U], &buf[i]) != RT_EOK)
        {
            rt_kprintf("Invalid byte: %s\n", argv[i + 3U]);
            rt_free(buf);
            return RT_ERROR;
        }
    }

    result = at24cxx_write(dev, addr, buf, (uint16_t)length);
    rt_free(buf);

    if (result == RT_EOK)
    {
        rt_kprintf("AT24 write ok: addr=0x%08x len=%u\n",
                   (unsigned int)addr,
                   (unsigned int)length);
    }
    else
    {
        rt_kprintf("AT24 write failed: addr=0x%08x len=%u\n",
                   (unsigned int)addr,
                   (unsigned int)length);
    }

    return result;
}

/**
 * @brief Fill a specified EEPROM range with one byte value.
 *
 * @param dev AT24CXX device handle.
 * @param addr EEPROM physical start address.
 * @param length Number of bytes to fill.
 * @param value Byte value to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_fill_data(at24cxx_device_t dev,
                                  uint32_t addr,
                                  uint32_t length,
                                  uint8_t value)
{
    uint8_t buf[AT24CXX_FILL_CHUNK_BYTES];
    uint32_t remain;
    rt_err_t result = RT_EOK;

    RT_ASSERT(dev);

    if (at24cxx_check_range(addr, length) != RT_EOK)
    {
        return RT_ERROR;
    }

    memset(buf, value, sizeof(buf));
    remain = length;

    while (remain > 0U)
    {
        uint16_t chunk;

        chunk = (remain > AT24CXX_FILL_CHUNK_BYTES) ? AT24CXX_FILL_CHUNK_BYTES : (uint16_t)remain;
        result = at24cxx_page_write(dev, addr, buf, chunk);
        if (result != RT_EOK)
        {
            rt_kprintf("AT24 fill failed at 0x%08x len=%u\n",
                       (unsigned int)addr,
                       (unsigned int)chunk);
            return result;
        }

        addr += chunk;
        remain -= chunk;
    }

    rt_kprintf("AT24 fill ok: addr=0x%08x len=%u value=0x%02x\n",
               (unsigned int)(addr - length),
               (unsigned int)length,
               (unsigned int)value);
    return RT_EOK;
}

#ifdef PKG_AT24CXX_USING_TEST_CMD
/** Fixed payload used by legacy destructive MSH test commands. */
static uint8_t at24cxx_test_buffer[] = "WELCOM TO RTT";

/** Size of the legacy destructive MSH test payload. */
#define AT24CXX_TEST_SIZE ((uint16_t)sizeof(at24cxx_test_buffer))

/**
 * @brief Run the legacy AT24CXX presence test command.
 *
 * @param dev AT24CXX device handle.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_test_check(at24cxx_device_t dev)
{
    rt_err_t result;

    RT_ASSERT(dev);

    result = at24cxx_check(dev);
    if (result == RT_EOK)
    {
        rt_kprintf("AT24 test_check ok\n");
    }
    else
    {
        rt_kprintf("AT24 test_check failed\n");
    }

    return result;
}

/**
 * @brief Read the legacy fixed test payload area from EEPROM address 0.
 *
 * @param dev AT24CXX device handle.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_test_read(at24cxx_device_t dev)
{
    uint8_t testbuffer[AT24CXX_TEST_SIZE + 1U];
    rt_err_t result;

    RT_ASSERT(dev);

    memset(testbuffer, 0, sizeof(testbuffer));
    result = at24cxx_read(dev, 0U, testbuffer, AT24CXX_TEST_SIZE);
    if (result == RT_EOK)
    {
        rt_kprintf("AT24 test_read: %s\n", testbuffer);
    }
    else
    {
        rt_kprintf("AT24 test_read failed\n");
    }

    return result;
}

/**
 * @brief Write the legacy fixed test payload to EEPROM address 0.
 *
 * @param dev AT24CXX device handle.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
static rt_err_t at24cxx_test_write(at24cxx_device_t dev)
{
    rt_err_t result;

    RT_ASSERT(dev);

    result = at24cxx_write(dev, 0U, at24cxx_test_buffer, AT24CXX_TEST_SIZE);
    if (result == RT_EOK)
    {
        rt_kprintf("AT24 test_write ok\n");
    }
    else
    {
        rt_kprintf("AT24 test_write failed\n");
    }

    return result;
}
#endif /* PKG_AT24CXX_USING_TEST_CMD */

/**
 * @brief AT24CXX MSH command entry.
 *
 * @param argc MSH argument count.
 * @param argv MSH argument vector.
 */
void at24cxx(int argc, char *argv[])
{
    static at24cxx_device_t dev = RT_NULL;

    if (argc <= 1)
    {
        at24cxx_show_usage();
        return;
    }

    if (!strcmp(argv[1], "probe"))
    {
        uint32_t addr_input;
        at24cxx_device_t new_dev;

        if ((argc != 4) ||
            (at24cxx_parse_u32(argv[3], &addr_input) != RT_EOK) ||
            (addr_input > 0xffU))
        {
            rt_kprintf("Usage: at24cxx probe <dev_name> <AddrInput>\n");
            return;
        }

        if ((dev != RT_NULL) &&
            (strcmp(dev->i2c->parent.parent.name, argv[2]) == 0) &&
            (dev->AddrInput == (uint8_t)addr_input))
        {
            rt_kprintf("AT24 probe ok: bus=%s addr_input=%u\n",
                       dev->i2c->parent.parent.name,
                       (unsigned int)dev->AddrInput);
            return;
        }

        new_dev = at24cxx_init(argv[2], (uint8_t)addr_input);
        if (new_dev == RT_NULL)
        {
            rt_kprintf("AT24 probe failed: bus=%s addr_input=%u\n",
                       argv[2],
                       (unsigned int)addr_input);
            return;
        }

        if (dev != RT_NULL)
        {
            at24cxx_deinit(dev);
        }

        dev = new_dev;

        rt_kprintf("AT24 probe ok: bus=%s addr_input=%u\n",
                   dev->i2c->parent.parent.name,
                   (unsigned int)dev->AddrInput);
        return;
    }

    if (!strcmp(argv[1], "info"))
    {
        at24cxx_print_info(dev);
        return;
    }

    if (!strcmp(argv[1], "dump") || !strcmp(argv[1], "read"))
    {
        uint32_t addr;
        uint32_t length;

        if (at24cxx_is_ready(dev) == RT_FALSE)
        {
            return;
        }

        if ((argc != 4) ||
            (at24cxx_parse_u32(argv[2], &addr) != RT_EOK) ||
            (at24cxx_parse_u32(argv[3], &length) != RT_EOK))
        {
            rt_kprintf("Usage: at24cxx %s <addr> <len>\n", argv[1]);
            return;
        }

        at24cxx_dump_data(dev, addr, length);
        return;
    }

    if (!strcmp(argv[1], "write"))
    {
        uint32_t addr;

        if (at24cxx_is_ready(dev) == RT_FALSE)
        {
            return;
        }

        if ((argc < 4) || (at24cxx_parse_u32(argv[2], &addr) != RT_EOK))
        {
            rt_kprintf("Usage: at24cxx write <addr> <byte0> [byte1] ...\n");
            return;
        }

        at24cxx_write_msh_bytes(dev, addr, argc, argv);
        return;
    }

    if (!strcmp(argv[1], "fill"))
    {
        uint32_t addr;
        uint32_t length;
        uint8_t value;

        if (at24cxx_is_ready(dev) == RT_FALSE)
        {
            return;
        }

        if ((argc != 5) ||
            (at24cxx_parse_u32(argv[2], &addr) != RT_EOK) ||
            (at24cxx_parse_u32(argv[3], &length) != RT_EOK) ||
            (at24cxx_parse_u8(argv[4], &value) != RT_EOK))
        {
            rt_kprintf("Usage: at24cxx fill <addr> <len> <byte>\n");
            return;
        }

        at24cxx_fill_data(dev, addr, length, value);
        return;
    }

#ifdef PKG_AT24CXX_USING_TEST_CMD
    if (!strcmp(argv[1], "test_check"))
    {
        if (at24cxx_is_ready(dev) == RT_TRUE)
        {
            at24cxx_test_check(dev);
        }
        return;
    }

    if (!strcmp(argv[1], "test_read"))
    {
        if (at24cxx_is_ready(dev) == RT_TRUE)
        {
            at24cxx_test_read(dev);
        }
        return;
    }

    if (!strcmp(argv[1], "test_write"))
    {
        if (at24cxx_is_ready(dev) == RT_TRUE)
        {
            at24cxx_test_write(dev);
        }
        return;
    }
#endif /* PKG_AT24CXX_USING_TEST_CMD */

    rt_kprintf("Unknown command: %s\n", argv[1]);
    at24cxx_show_usage();
}
MSH_CMD_EXPORT(at24cxx, at24cxx eeprom function);
#endif /* PKG_AT24CXX_FINSH */
#endif /* PKG_USING_AT24CXX */
