/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-13     XiaojieFan   the first version
 * 2019-12-04     RenMing      Use PAGE WRITE instead of BYTE WRITE and input address can be selected
 * 2026-02-01     CXSforHPU    Declare the reverse initialization function in the header file
 */

#ifndef __AT24CXX_H__
#define __AT24CXX_H__

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define AT24C01   0
#define AT24C02   1
#define AT24C04   2
#define AT24C08   3
#define AT24C16   4
#define AT24C32   5
#define AT24C64   6
#define AT24C128  7
#define AT24C256  8
#define AT24C512  9
#define AT24CTYPE 10   // Number of supported types

#define EE_TWR 5

#ifndef PKG_AT24CXX_EE_TYPE
#define PKG_AT24CXX_EE_TYPE AT24C02
#endif

#define AT24CXX_A0 (1 << 0)
#define AT24CXX_A1 (1 << 1)
#define AT24CXX_A2 (1 << 2)

#if (PKG_AT24CXX_EE_TYPE == AT24C01)
#define AT24CXX_PAGE_BYTE       8
#define AT24CXX_MAX_MEM_ADDRESS 128
#elif (PKG_AT24CXX_EE_TYPE == AT24C02)
#define AT24CXX_PAGE_BYTE       8
#define AT24CXX_MAX_MEM_ADDRESS 256
#elif (PKG_AT24CXX_EE_TYPE == AT24C04)
#define AT24CXX_PAGE_BYTE       16
#define AT24CXX_MAX_MEM_ADDRESS 512
#elif (PKG_AT24CXX_EE_TYPE == AT24C08)
#define AT24CXX_PAGE_BYTE       16
#define AT24CXX_MAX_MEM_ADDRESS 1024
#elif (PKG_AT24CXX_EE_TYPE == AT24C16)
#define AT24CXX_PAGE_BYTE       16
#define AT24CXX_MAX_MEM_ADDRESS 2048
#elif (PKG_AT24CXX_EE_TYPE == AT24C32)
#define AT24CXX_PAGE_BYTE       32
#define AT24CXX_MAX_MEM_ADDRESS 4096
#elif (PKG_AT24CXX_EE_TYPE == AT24C64)
#define AT24CXX_PAGE_BYTE       32
#define AT24CXX_MAX_MEM_ADDRESS 8192
#elif (PKG_AT24CXX_EE_TYPE == AT24C128)
#define AT24CXX_PAGE_BYTE       64
#define AT24CXX_MAX_MEM_ADDRESS 16384
#elif (PKG_AT24CXX_EE_TYPE == AT24C256)
#define AT24CXX_PAGE_BYTE       64
#define AT24CXX_MAX_MEM_ADDRESS 32768
#elif (PKG_AT24CXX_EE_TYPE == AT24C512)
#define AT24CXX_PAGE_BYTE       128
#define AT24CXX_MAX_MEM_ADDRESS 65536
#endif

/**
 * @brief AT24CXX device object.
 */
struct at24cxx_device
{
    /** I2C bus device used to access the EEPROM. */
    struct rt_i2c_bus_device *i2c;

    /** Mutex handle used to serialize EEPROM operations. */
    rt_mutex_t lock;

    /** Address input bits or bank select value. */
    uint8_t AddrInput;
};
typedef struct at24cxx_device *at24cxx_device_t;

/**
 * @brief Bind a caller-provided AT24CXX device object and mutex.
 *
 * This API does not allocate the device object, create a mutex, initialize the
 * supplied mutex, detach the supplied mutex, or delete the supplied mutex. The
 * caller owns @p dev and @p lock, and both objects must remain valid until all
 * AT24CXX operations using @p dev have stopped.
 *
 * @param dev Caller-provided AT24CXX device object.
 * @param lock Caller-provided initialized mutex handle.
 * @param i2c_bus_name I2C bus device name.
 * @param AddrInput Address input bits or bank select value.
 *
 * @return RT_EOK on success, otherwise RT_ERROR.
 */
extern rt_err_t at24cxx_init_device(at24cxx_device_t dev, rt_mutex_t lock, const char *i2c_bus_name, uint8_t AddrInput);

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
extern at24cxx_device_t at24cxx_init(const char *i2c_bus_name, uint8_t AddrInput);

/**
 * @brief Deinitialize a caller-provided AT24CXX device object.
 *
 * This API only clears the binding created by at24cxx_init_device(). It does
 * not detach, delete, or otherwise release the caller-provided mutex, and it
 * does not free @p dev.
 *
 * @param dev Caller-provided AT24CXX device handle to unbind.
 */
extern void at24cxx_deinit_device(at24cxx_device_t dev);

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
extern void at24cxx_deinit(at24cxx_device_t dev);

/**
 * @brief Check whether the AT24CXX EEPROM is accessible.
 *
 * @param dev AT24CXX device handle.
 *
 * @return RT_EOK if the EEPROM is accessible, otherwise RT_ERROR.
 */
extern rt_err_t at24cxx_check(at24cxx_device_t dev);

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
extern rt_err_t at24cxx_check_timeout(at24cxx_device_t dev, rt_int32_t timeout);

/**
 * @brief Read bytes from a specified EEPROM physical address.
 *
 * This compatibility API waits forever for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param ReadAddr EEPROM physical start address.
 * @param pBuffer Buffer used to store read bytes.
 * @param NumToRead Number of bytes to read.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
extern rt_err_t at24cxx_read(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);

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
extern rt_err_t at24cxx_read_timeout(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead, rt_int32_t timeout);

/**
 * @brief Write bytes to a specified EEPROM physical address.
 *
 * This compatibility API waits forever for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param WriteAddr EEPROM physical start address.
 * @param pBuffer Buffer that stores bytes to write.
 * @param NumToWrite Number of bytes to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
extern rt_err_t at24cxx_write(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);

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
extern rt_err_t at24cxx_write_timeout(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite, rt_int32_t timeout);

/**
 * @brief Page-read bytes from a specified EEPROM physical address.
 *
 * This compatibility API waits forever for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param ReadAddr EEPROM physical start address.
 * @param pBuffer Buffer used to store read bytes.
 * @param NumToRead Number of bytes to read.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
extern rt_err_t at24cxx_page_read(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);

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
extern rt_err_t at24cxx_page_read_timeout(at24cxx_device_t dev, uint32_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead, rt_int32_t timeout);

/**
 * @brief Page-write bytes to a specified EEPROM physical address.
 *
 * This compatibility API waits forever for the device mutex.
 *
 * @param dev AT24CXX device handle.
 * @param WriteAddr EEPROM physical start address.
 * @param pBuffer Buffer that stores bytes to write.
 * @param NumToWrite Number of bytes to write.
 *
 * @return RT_EOK on success, otherwise RT_ERROR or the mutex error code.
 */
extern rt_err_t at24cxx_page_write(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);

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
extern rt_err_t at24cxx_page_write_timeout(at24cxx_device_t dev, uint32_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite, rt_int32_t timeout);

#endif /* __AT24CXX_H__ */
