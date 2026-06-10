# AT24CXX 软件包

## 1 介绍

AT24CXX 软件包提供 AT24CXX 系列 EEPROM 的基础读写能力，并提供
`Finsh/MSH` 调试命令。

目前已在 AT24C02、AT24C512 上验证通过。

### 1.1 目录结构

| 名称 | 说明 |
| ---- | ---- |
| at24cxx.h | EEPROM 使用头文件 |
| at24cxx.c | EEPROM 使用源代码 |
| SConscript | RT-Thread 默认构建脚本 |
| README.md | 软件包使用说明 |
| at24cxx_datasheet.pdf | 官方数据手册 |

### 1.2 许可证

AT24CXX 软件包遵循 Apache-2.0 许可证，详见 LICENSE 文件。

### 1.3 依赖

依赖 `RT-Thread I2C` 设备驱动框架。

## 2 获取软件包

在 RT-Thread 包管理器中选择：

```text
RT-Thread online packages
    peripheral libraries and drivers  --->
        [*] at24cxx: eeprom at24cxx driver library  --->
            Version (latest)  --->
```

配置说明：

| 配置项 | 说明 |
| ------ | ---- |
| `PKG_USING_AT24CXX` | 启用 AT24CXX 软件包 |
| `PKG_AT24CXX_FINSH` | 启用 AT24CXX Finsh/MSH 命令 |
| `PKG_AT24CXX_USING_TEST_CMD` | 启用破坏性 legacy 测试命令，默认关闭 |
| `PKG_AT24CXX_EE_TYPE` | 选择 EEPROM 型号 |
| `PKG_AT24CXX_VER` | 选择软件包版本 |

配置完成后，使用 `pkgs --update` 更新包到 BSP 中。

## 3 使用 at24cxx 软件包

### 3.1 API

#### 3.1.1 初始化

```c
rt_err_t at24cxx_init_device(at24cxx_device_t dev,
                             rt_mutex_t lock,
                             const char *i2c_bus_name,
                             uint8_t AddrInput);

at24cxx_device_t at24cxx_init(const char *i2c_bus_name, uint8_t AddrInput);
```

根据 I2C 总线名称和地址输入位初始化 AT24CXX 设备。

| 参数 | 描述 |
| ---- | ---- |
| `dev` | 调用方提供的 `struct at24cxx_device` 对象，仅 `at24cxx_init_device()` 使用 |
| `lock` | 调用方提供并已初始化的互斥锁，仅 `at24cxx_init_device()` 使用 |
| `i2c_bus_name` | I2C 总线设备名称，例如 `i2c1` |
| `AddrInput` | A0/A1/A2 或分区地址输入值 |
| `at24cxx_init_device()` 返回值 | `RT_EOK` 表示成功，其他值表示失败 |
| `at24cxx_init()` 返回值 | `!= RT_NULL` 表示成功，`RT_NULL` 表示失败 |

`at24cxx_init_device()` 用于无动态分配场景。调用方负责提供并维护
`struct at24cxx_device` 对象和互斥锁。驱动只绑定外部对象，不调用
`rt_calloc()`、`rt_mutex_create()`、`rt_mutex_init()`、`rt_mutex_detach()`
或 `rt_mutex_delete()`：

```c
static struct at24cxx_device eeprom_dev;
static struct rt_mutex eeprom_lock;

if (rt_mutex_init(&eeprom_lock, "at24", RT_IPC_FLAG_FIFO) != RT_EOK)
{
    /* handle mutex init failure */
}

if (at24cxx_init_device(&eeprom_dev, &eeprom_lock, "i2c1", 0) != RT_EOK)
{
    /* handle init failure */
}
```

`at24cxx_init()` 是兼容旧版本的接口，仍会在内部动态分配
`struct at24cxx_device` 对象并创建互斥锁；旧代码无需修改。

`at24cxx_init_device()` 成功后应使用 `at24cxx_deinit_device()` 解绑；
`at24cxx_init()` 成功后应使用 `at24cxx_deinit()` 释放。

#### 3.1.2 反初始化

```c
void at24cxx_deinit_device(at24cxx_device_t dev);
void at24cxx_deinit(at24cxx_device_t dev);
```

两种初始化方式对应两种反初始化接口：

| 初始化方式 | 反初始化接口 | 行为 |
| ---- | ---- | ---- |
| `at24cxx_init_device()` | `at24cxx_deinit_device()` | 只清空外部 `dev` 绑定关系，不释放 `dev`，不 detach/delete 外部 `mutex` |
| `at24cxx_init()` | `at24cxx_deinit()` | 删除内部创建的互斥锁，并释放动态分配的 `dev` |

外部对象路径示例：

```c
at24cxx_deinit_device(&eeprom_dev);
rt_mutex_detach(&eeprom_lock);
```

动态分配路径示例：

```c
at24cxx_deinit(dev);
```

不要对 `at24cxx_init_device()` 绑定的外部对象调用 `at24cxx_deinit()`，
否则会把外部对象当作动态对象释放。

#### 3.1.3 读取

```c
rt_err_t at24cxx_read(at24cxx_device_t dev,
                      uint32_t ReadAddr,
                      uint8_t *pBuffer,
                      uint16_t NumToRead);

rt_err_t at24cxx_read_timeout(at24cxx_device_t dev,
                              uint32_t ReadAddr,
                              uint8_t *pBuffer,
                              uint16_t NumToRead,
                              rt_int32_t timeout);

rt_err_t at24cxx_page_read(at24cxx_device_t dev,
                           uint32_t ReadAddr,
                           uint8_t *pBuffer,
                           uint16_t NumToRead);

rt_err_t at24cxx_page_read_timeout(at24cxx_device_t dev,
                                   uint32_t ReadAddr,
                                   uint8_t *pBuffer,
                                   uint16_t NumToRead,
                                   rt_int32_t timeout);
```

从 EEPROM 指定物理地址开始读取指定字节数。

| 参数 | 描述 |
| ---- | ---- |
| `dev` | AT24CXX 设备对象 |
| `ReadAddr` | EEPROM 物理起始地址 |
| `pBuffer` | 读取缓冲区 |
| `NumToRead` | 读取长度 |
| `timeout` | 等待 AT24CXX 设备互斥锁的最大 tick 数，仅 timeout API 使用 |
| 返回值 | `RT_EOK` 表示成功，其他值表示失败或互斥锁等待失败 |

`at24cxx_read()` 和 `at24cxx_page_read()` 是兼容旧版本的接口，内部使用
`RT_WAITING_FOREVER` 等待互斥锁。需要限制等待时间时，使用对应的
`*_timeout()` 接口。

#### 3.1.4 写入

```c
rt_err_t at24cxx_write(at24cxx_device_t dev,
                       uint32_t WriteAddr,
                       uint8_t *pBuffer,
                       uint16_t NumToWrite);

rt_err_t at24cxx_write_timeout(at24cxx_device_t dev,
                               uint32_t WriteAddr,
                               uint8_t *pBuffer,
                               uint16_t NumToWrite,
                               rt_int32_t timeout);

rt_err_t at24cxx_page_write(at24cxx_device_t dev,
                            uint32_t WriteAddr,
                            uint8_t *pBuffer,
                            uint16_t NumToWrite);

rt_err_t at24cxx_page_write_timeout(at24cxx_device_t dev,
                                    uint32_t WriteAddr,
                                    uint8_t *pBuffer,
                                    uint16_t NumToWrite,
                                    rt_int32_t timeout);
```

从 EEPROM 指定物理地址开始写入指定字节数。

| 参数 | 描述 |
| ---- | ---- |
| `dev` | AT24CXX 设备对象 |
| `WriteAddr` | EEPROM 物理起始地址 |
| `pBuffer` | 写入缓冲区 |
| `NumToWrite` | 写入长度 |
| `timeout` | 等待 AT24CXX 设备互斥锁的最大 tick 数，仅 timeout API 使用 |
| 返回值 | `RT_EOK` 表示成功，其他值表示失败或互斥锁等待失败 |

`at24cxx_write()` 和 `at24cxx_page_write()` 是兼容旧版本的接口，内部使用
`RT_WAITING_FOREVER` 等待互斥锁。需要限制等待时间时，使用对应的
`*_timeout()` 接口。

#### 3.1.5 timeout 接口兼容策略

旧 API 保持原型不变，默认永久等待内部互斥锁，因此旧代码无需修改：

```c
rt_err_t result;

result = at24cxx_write(dev, 0x1000, buf, len);
```

新 API 允许调用方传入互斥锁等待超时时间，适合自动化测试、CI、低优先级任务
或不允许长时间阻塞的业务线程：

```c
rt_err_t result;

result = at24cxx_write_timeout(dev, 0x1000, buf, len,
                               rt_tick_from_millisecond(10));
if (result != RT_EOK)
{
    /* handle mutex timeout or I2C/write failure */
}
```

`timeout` 的含义与 RT-Thread `rt_mutex_take()` 保持一致：

| 取值 | 行为 |
| ---- | ---- |
| `RT_WAITING_FOREVER` | 永久等待，兼容旧 API 行为 |
| `0` | 不等待，立即尝试获取互斥锁 |
| `> 0` | 最多等待指定 tick 数 |

#### 3.1.6 设备检查

```c
rt_err_t at24cxx_check(at24cxx_device_t dev);

rt_err_t at24cxx_check_timeout(at24cxx_device_t dev,
                               rt_int32_t timeout);
```

通过最后一个 EEPROM 字节执行读写检查。

| 参数        | 描述                                                        |
| --------- | --------------------------------------------------------- |
| `dev`     | AT24CXX 设备对象                                              |
| `timeout` | 等待 AT24CXX 设备互斥锁的最大 tick 数，仅 `at24cxx_check_timeout()` 使用 |
| 返回值       | `RT_EOK` 表示设备可访问，其他值表示失败或互斥锁等待失败                          |

`at24cxx_check()` 是兼容旧版本的接口，内部使用 `RT_WAITING_FOREVER`
等待互斥锁。需要限制等待时间时，使用 `at24cxx_check_timeout()`。

示例：

```c
rt_err_t result;

result = at24cxx_check_timeout(dev, rt_tick_from_millisecond(10));
if (result != RT_EOK)
{
    /* handle mutex timeout or EEPROM access failure */
}
```

注意：该 API 在最后一个 EEPROM 字节不是 `0x55` 时会写入 `0x55`，
因此它不是只读检查接口。

## 4 Finsh/MSH 命令

启用 `PKG_AT24CXX_FINSH` 后，可以使用 `at24cxx` 命令访问 EEPROM。

### 4.1 命令列表

```text
Usage:
  at24cxx probe <dev_name> <AddrInput>
  at24cxx info
  at24cxx dump <addr> <len>
  at24cxx read <addr> <len>
  at24cxx write <addr> <byte0> [byte1] ...
  at24cxx fill <addr> <len> <byte>
Examples:
  at24cxx probe i2c1 0
  at24cxx dump 0x1000 0x40
  at24cxx write 0x1017 0x00
  at24cxx fill 0x1000 0x20 0xff
```

### 4.2 探测设备

```text
msh />at24cxx probe i2c1 0
AT24 probe ok: bus=i2c1 addr_input=0
```

`AddrInput` 用于设置 AT24CXX 的地址输入位或分区选择值。不同 EEPROM
型号对 `AddrInput` 的有效范围不同，驱动会在初始化阶段检查。

对 AT24C04/AT24C08/AT24C16，`AddrInput` 仍按旧版本语义用于选择
256 字节 block/bank；切换 block/bank 时重新执行 `probe` 即可。

### 4.3 查看设备信息

```text
msh />at24cxx info
AT24CXX info:
  bus        : i2c1
  addr_input : 0
  type       : AT24C512
  size       : 65536 bytes
  page       : 128 bytes
```

### 4.4 AT24C04/AT24C08/AT24C16 地址说明

为保持旧版本兼容，AT24C04/AT24C08/AT24C16 仍使用固定
`AddrInput` 选择 256 字节 block/bank。`read`、`dump`、`write`、
`fill` 命令中的 `<addr>` 表示当前 block/bank 内偏移地址，不会自动
根据全局物理地址切换 block。

示例：

```shell
# AT24C04 低 256B
msh />at24cxx probe i2c1 0
msh />at24cxx dump 0x00 0x40

# AT24C04 高 256B
msh />at24cxx probe i2c1 1
msh />at24cxx dump 0x00 0x40
```

AT24C32 及以上型号使用 16-bit word address，`<addr>` 可按 EEPROM
全局物理地址使用。


### 4.5 读取或 dump 指定区域

`read` 和 `dump` 都按十六进制格式输出 EEPROM 原始字节。

```text
msh />at24cxx dump 0x1000 0x40
msh />at24cxx read 0x1000 0x40
```

### 4.6 写指定字节

```text
msh />at24cxx write 0x1017 0x00
```

该命令从 `0x1017` 开始写入一个字节 `0x00`。

也可以连续写入多个字节：

```text
msh />at24cxx write 0x1000 0x12 0x34 0x56 0x78
```

### 4.7 填充指定区域

```text
msh />at24cxx fill 0x1000 0x20 0xff
```

该命令从 `0x1000` 开始连续写入 `0x20` 个 `0xff`。

### 4.8 NVM slot 验证示例

假设参数模块在 AT24CXX 上的起始地址为 `0x1000`，CRC 字节偏移为
`0x17`，可以按下面步骤破坏 CRC 并验证恢复逻辑：

```text
msh />at24cxx probe i2c1 0
msh />at24cxx dump 0x1000 0x40
msh />at24cxx write 0x1017 0x00
msh />at24cxx dump 0x1000 0x40
```

之后重启系统，或触发参数模块的 NVM restore 流程，观察是否正确识别
slot CRC 异常并回退到有效 slot 或默认值。

## 5 破坏性 legacy 测试命令

启用 `PKG_AT24CXX_USING_TEST_CMD` 后，额外提供：

```text
at24cxx test_check
at24cxx test_read
at24cxx test_write
```

这些命令用于保留旧的手工测试行为：

| 命令 | 行为 |
| ---- | ---- |
| `test_check` | 调用 `at24cxx_check()`，可能写 EEPROM 最后一个字节 |
| `test_read` | 从 EEPROM 地址 `0` 读取固定测试字符串区域 |
| `test_write` | 向 EEPROM 地址 `0` 写入固定测试字符串 |

正常 NVM 验证不建议启用该选项，避免误写 bootloader 或其他模块保留区。

## 6 注意事项

- 写命令会直接修改 EEPROM 物理地址，执行前必须确认该地址属于当前模块。
- 验证 NVM slot 时，只操作参数模块拥有的 EEPROM 窗口，不要覆盖低地址旧区。
- `at24cxx_check()` 不是只读接口，它可能写 EEPROM 最后一个字节。
- 从设备地址为 7 位地址 `0x50`，不是 8 位地址 `0xA0`。
- AT24C04/AT24C08/AT24C16 为保持旧版本兼容，仍通过 `AddrInput`手动选择 256 字节 block/bank；`<addr>` 表示当前 block/bank 内偏移，不会自动按全局物理地址切换 block。
- 页写操作要求第一帧为从机地址和 EEPROM 地址，第二帧为数据，两帧之间不能释放 I2C 总线。
- `EE_TWR` 应按 EEPROM 数据手册配置为写周期时间，默认值为 `5 ms`。
- 无动态分配场景优先使用 `at24cxx_init_device()`，并确保外部设备对象
  的生命周期覆盖所有 AT24CXX 读写调用。

## 7 联系方式

* 维护：[XiaojieFan](https://github.com/XiaojieFan)
* 主页：https://github.com/XiaojieFan/at24cxx
