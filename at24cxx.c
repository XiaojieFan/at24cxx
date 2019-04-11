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

//#ifdef PKG_USING_AT24CXX
#define AT24CXX_ADDR 0x50                      //A0 A1 A2 connect GND
#define AT24CXX_I2C_BUS_NAME          "i2c1"  /*I2C总线设备名称*/

static rt_err_t write_reg(struct rt_i2c_bus_device *bus, rt_uint8_t reg, rt_uint8_t *data)
{
    rt_uint8_t buf[3];

    buf[0] = reg; //cmd
    buf[1] = data[0];
    //buf[2] = data[1];
    if (buf[1] == 0)
		{
			if(rt_i2c_master_send(bus, AT24CXX_ADDR, 0, &buf[0], 1) == 1)
				return RT_EOK;
			else
				return -RT_ERROR;
		}
		else
		{
      if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 2) == 2)
        return RT_EOK;
      else
        return -RT_ERROR;
	  }
}

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
uint8_t at24cxx_read_one_byte(struct rt_i2c_bus_device *bus,uint8_t readAddr)
{
	rt_uint8_t buf[2];
	rt_uint8_t temp;
	buf[0] = readAddr;
  if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 1) == 0)
	{
      return -RT_ERROR;	
  }		
   read_regs(bus, 1, &temp);
	 return temp;	
}

rt_err_t at24cxx_write_one_byte(struct rt_i2c_bus_device *bus,uint8_t writeAddr,uint8_t dataToWrite)
{
	  rt_uint8_t buf[2];

    buf[0] = writeAddr; //cmd
    buf[1] = dataToWrite;
    //buf[2] = data[1];

	
    if (rt_i2c_master_send(bus, AT24CXX_ADDR, 0, buf, 2) == 2)
      return RT_EOK;
    else
      return -RT_ERROR;	   
	
}
void at24cxx_writeLenByte(struct rt_i2c_bus_device *bus,uint8_t writeAddr,uint32_t dataToWrite,uint8_t len)
{
	uint8_t t;
	for (t=0;t<len;t++)
	{
		at24cxx_write_one_byte(bus,writeAddr + t,(dataToWrite>>(8*t))&0xff);
	}
}
uint32_t at24cxx_readLenByte(struct rt_i2c_bus_device *bus,uint8_t readAddr,uint8_t len)
{
	uint8_t t;
	uint32_t temp = 0;
	for (t=0;t<len;t++)
	{
		temp <<=8;
		temp += at24cxx_read_one_byte(bus,readAddr+len -t -1);
	}
	return temp;
}
rt_err_t at24cxx_check(struct rt_i2c_bus_device *bus)
{
	uint8_t temp;
	temp = at24cxx_read_one_byte(bus,255);
	if(temp == 0x55) return 0;
	else
	{
		at24cxx_write_one_byte(bus,255,0x55);
		temp = at24cxx_read_one_byte(bus,255);
		if(temp == 0x55) return 0;
	}
	return 1;
}
//在AT24CXX里面的指定地址开始读出指定个数的数据
//ReadAddr :开始读出的地址 对24c02为0~255
//pBuffer  :数据数组首地址
//NumToRead:要读出数据的个数
rt_err_t at24cxx_read(struct rt_i2c_bus_device *bus,uint8_t ReadAddr,uint8_t *pBuffer,uint16_t NumToRead)
{
	while(NumToRead)
	{
		*pBuffer++=at24cxx_read_one_byte(bus,ReadAddr++);	
		NumToRead--;
	}
	return RT_EOK;
}  
//在AT24CXX里面的指定地址开始写入指定个数的数据
//WriteAddr :开始写入的地址 对24c02为0~255
//pBuffer   :数据数组首地址
//NumToWrite:要写入数据的个数
rt_err_t at24cxx_write(struct rt_i2c_bus_device *bus,uint8_t WriteAddr,uint8_t *pBuffer,uint16_t NumToWrite)
{
	uint8_t i=0;
	while(1)//NumToWrite--
	{
		//at24cxx_write_one_byte(bus,WriteAddr,*pBuffer);
		if (at24cxx_write_one_byte(bus,WriteAddr,pBuffer[i]) != RT_EOK)
		{
			rt_thread_mdelay(1);
		}
		else
		{
		WriteAddr++;
		i++;
		}
		if(WriteAddr == NumToWrite)
		{
			break;
		}
	 
	}
	return RT_EOK;
}
/**
 * This function initializes aht10 registered device driver
 *
 * @param dev the name of aht10 device
 *
 * @return the aht10 device.
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
//57 45 4C 43 4F 4D 20 54 4F 20 49 57 41 4C 4C
uint8_t TEST_BUFFER[]="WELCOM TO IWALL";
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
              at24cxx_read(dev->i2c,0,testbuffer,SIZE);

              rt_kprintf("read at24cxx : %s\n", testbuffer);
							
            }
            else
            {
                rt_kprintf("Please using 'at24cxx probe <dev_name>' first\n");
            }
        }
				else if (!strcmp(argv[1],"write"))
				{
					at24cxx_write(dev->i2c,0,TEST_BUFFER,SIZE);
					rt_kprintf("write ok\n");
				}
				else if (!strcmp(argv[1],"check"))
				{
					if( at24cxx_check(dev->i2c) == 1)
					{
						 rt_kprintf("check faild \n");
					}
				}
        else
        {
            rt_kprintf("Unknown command. Please enter 'aht10' for help\n");
        }
    }
    else
    {
        rt_kprintf("Usage:\n");
        rt_kprintf("at24cxx probe <dev_name>   - probe eeprom by given name\n");
        rt_kprintf("at24cxx read               - read eeprom at24cxx data\n");
				rt_kprintf("at24cxx write              - write eeprom at24cxx data\n");
    }
}
MSH_CMD_EXPORT(at24cxx, at24cxx eeprom function);
//#endif